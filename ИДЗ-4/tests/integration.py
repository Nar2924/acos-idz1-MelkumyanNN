#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
from queue import Empty, Queue
import signal
import socket
import struct
import subprocess
import tempfile
from threading import Thread
from time import monotonic

ROOT = Path(__file__).resolve().parents[1]
TIMEOUT = 8


class Process:
    def __init__(self, argv, directory, env=None, fds=()):
        self.process = subprocess.Popen(
            argv, cwd=directory, env=env, pass_fds=fds,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8",
        )
        self.lines = []
        self.messages = Queue()
        self.reader = Thread(target=self.read_lines, daemon=True)
        self.reader.start()

    def read_lines(self):
        for line in self.process.stdout:
            self.lines.append(line.rstrip())
            self.messages.put(self.lines[-1])
        self.messages.put(None)

    def expect(self, text):
        deadline = monotonic() + TIMEOUT
        try:
            while True:
                line = self.messages.get(timeout=max(0, deadline - monotonic()))
                if line is None:
                    break
                if text in line:
                    return line
        except Empty:
            pass
        raise AssertionError(f"Expected {text!r} from {self.process.args}:\n" + "\n".join(self.lines))

    def send(self, text):
        self.process.stdin.write(text)
        self.process.stdin.flush()

    def wait(self, expected=0):
        status = self.process.wait(timeout=TIMEOUT)
        self.reader.join(timeout=TIMEOUT)
        assert status == expected, (self.process.args, status, self.lines)

    def interrupt(self):
        self.process.send_signal(signal.SIGINT)

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=TIMEOUT)
        self.process.stdin.close()
        self.reader.join(timeout=TIMEOUT)
        self.process.stdout.close()


class Case:
    def __init__(self, binary_dir, inherited):
        self.binary_dir = binary_dir
        self.inherited = inherited
        self.processes = []
        self.endpoints = []
        self.signal_fd = None

    def __enter__(self):
        self.directory = tempfile.TemporaryDirectory()
        if self.inherited:
            self.port = "9000"
        else:
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                self.port = str(probe.getsockname()[1])
        return self

    def __exit__(self, *_):
        for process in reversed(self.processes):
            process.close()
        for endpoint, _ in self.endpoints:
            endpoint.close()
        if self.signal_fd is not None:
            os.close(self.signal_fd)
        self.directory.cleanup()

    def launch(self, program, args=(), env=None, fds=()):
        process = Process(
            [str(self.binary_dir / program), "127.0.0.1", self.port, *args],
            self.directory.name, env, fds,
        )
        self.processes.append(process)
        return process

    def server(self, config, *args):
        if self.inherited:
            listener, self.signal_fd = os.pipe()
            pairs = [socket.socketpair() for _ in range(12)]
            self.endpoints = [(peer, server.fileno()) for peer, server in pairs]
            env = {**os.environ, "TEST_LISTENER_FD": str(listener)}
            try:
                process = self.launch(
                    "garden_app", args, env, (listener, *(fd for _, fd in self.endpoints)),
                )
            finally:
                os.close(listener)
                for _, server in pairs:
                    server.close()
        else:
            process = self.launch("garden_app", args)
        process.send("".join(f"{value}\n" for value in config))
        process.expect("LISTEN ")
        return process

    def inherited_endpoint(self):
        endpoint, fd = self.endpoints.pop(0)
        os.write(self.signal_fd, struct.pack("@i", fd))
        return endpoint

    def client(self, program, *args):
        if self.inherited:
            with self.inherited_endpoint() as endpoint:
                fd = endpoint.fileno()
                return self.launch(program, args, {**os.environ, "TEST_CLIENT_FD": str(fd)}, (fd,))
        return self.launch(program, args)

    def raw(self):
        endpoint = self.inherited_endpoint() if self.inherited else socket.create_connection(
            ("127.0.0.1", int(self.port)), timeout=TIMEOUT,
        )
        endpoint.settimeout(TIMEOUT)
        return endpoint


def multiple_monitors(binary_dir, inherited):
    with Case(binary_dir, inherited) as case:
        server = case.server([3, 42, 20, 20, 40, 15, 16, 0.3, 2.4, 50], "--seed", "13")
        first = case.client("monitor_app")
        first.expect("SNAPSHOT phase=waiting")
        first.expect("SNAPSHOT_END")
        second = case.client("monitor_app")
        second.expect("SNAPSHOT_END")

        with case.raw() as fragmented, case.raw() as rejected:
            fragmented.sendall(b"MON")
            fragmented.sendall(b"ITOR\r\n")
            with fragmented.makefile("rb") as input_stream:
                assert input_stream.readline().startswith(b"SNAPSHOT phase=waiting")
            rejected.sendall(b"UNKNOWN\n")
            with rejected.makefile("rb") as input_stream:
                assert input_stream.readline().startswith(b"ERROR ")

        first_gardener = case.client("gardener_app", "101", "16")
        second_gardener = case.client("gardener_app", "102", "20")
        first_gardener.expect("WAIT ")
        second_gardener.expect("WAIT ")
        server.send("\n")
        first.expect("START gardeners=2")
        first.expect("WATER_START ")
        first.expect("WATER_START ")
        second.expect("WATER_START ")
        first.interrupt()
        first.wait(-signal.SIGINT)

        reconnected = case.client("monitor_app")
        snapshot = reconnected.expect("SNAPSHOT phase=running")
        reconnected.expect("SNAPSHOT_END")
        assert any("gardener=101" in line and line.startswith("FLOWER ") for line in reconnected.lines)
        observed = reconnected.expect("WATER_DONE ")
        final = reconnected.expect("END reason=duration time=2.400")
        for process in (server, second, reconnected, first_gardener, second_gardener):
            process.wait()
        assert final in second.lines and final in server.lines
        first_assignments = {
            next(line for line in process.lines if line.startswith("WATERING ")).split()[1]
            for process in (first_gardener, second_gardener)
        }
        assert first_assignments == {"flower=0", "flower=1"}
        assert any("WATERED -> NORMAL" in line for line in second.lines)
        index = reconnected.lines.index("SNAPSHOT_END")
        events_after_snapshot = reconnected.lines[index + 1:]
        assert events_after_snapshot == second.lines[-len(events_after_snapshot):]
        print("PASS two observers, reconnect while watering, equal events and final summary")
        print(snapshot)
        print(observed)
        print(final)


def replace_gardener(binary_dir, inherited):
    with Case(binary_dir, inherited) as case:
        server = case.server([1, 42, 5, 5, 40, 15, 20, 1, 1.8, 25], "--gardeners", "1")
        monitor = case.client("monitor_app")
        monitor.expect("SNAPSHOT_END")
        first = case.client("gardener_app", "7", "1")
        first.expect("WATERING flower=0")
        first.interrupt()
        first.wait(-signal.SIGINT)
        cancelled = monitor.expect("WATER_CANCEL gardener=7 flower=0")
        monitor.expect("GARDENER_LEFT id=7")
        replacement = case.client("gardener_app", "7", "50")
        replacement.expect("START time=")
        replacement.expect("DONE flower=0")
        for process in (server, monitor, replacement):
            process.wait()
        assert any("END reason=duration" in line and "cycles=1" in line for line in monitor.lines)
        print("PASS interrupted watering and gardener reconnect with the same ID")
        print(cancelled)


def protection_and_overwater(binary_dir, inherited):
    with Case(binary_dir, inherited) as case:
        server = case.server([1, 39, 0, 0, 40, 15, 4, 6, 0, 50], "--initial-state", "wilting")
        monitor = case.client("monitor_app")
        monitor.expect("FLOWER id=0 state=WILTING")
        with case.raw() as first, case.raw() as second, first.makefile("rb") as first_in, \
                second.makefile("rb") as second_in:
            for identifier, peer, stream in ((7, first, first_in), (8, second, second_in)):
                peer.sendall(f"GARDENER {identifier} 4\n".encode())
                assert stream.readline().startswith(b"CONNECTED ")
                assert stream.readline().startswith(b"WAIT ")
            server.send("\n")
            for peer, stream in ((first, first_in), (second, second_in)):
                assert stream.readline().startswith(b"START ")
                peer.sendall(b"READY\n")
                assert stream.readline().startswith(b"OPTION flower=0 moisture=39 cycles=0")
                assert stream.readline() == b"OPTIONS_END\n"

            first.sendall(b"SELECT 0 0\n")
            assert first_in.readline().startswith(b"WATERING flower=0")
            second.sendall(b"SELECT 0 0\n")
            assert second_in.readline().startswith(b"RETRY flower=0")
            assert first_in.readline().startswith(b"DONE flower=0")
            second.sendall(b"SELECT 0 0\n")
            assert second_in.readline().startswith(b"RETRY flower=0")
            second.sendall(b"SELECT 0 1\n")
            assert second_in.readline().startswith(b"OVERWATER flower=0")
            assert second_in.readline().startswith(b"END reason=all_dead")
            observed = monitor.expect("OVERWATER gardener=8 flower=0")
            final = monitor.expect("END reason=all_dead")
            assert "alive=0" in final and "overwater=1 cycles=1" in final
        for process in (server, monitor):
            process.wait()
        print("PASS concurrent and stale selections rejected, premature watering is fatal")
        print(observed)
        print(final)

    with Case(binary_dir, inherited) as case:
        server = case.server([1, 50, 0, 0, 40, 15, 4, 6, 0, 50], "--initial-state", "watered")
        monitor = case.client("monitor_app")
        flower = monitor.expect("FLOWER id=0 state=WATERED")
        assert "protected_until=6.000" in flower
        server.interrupt()
        for process in (server, monitor):
            process.wait()
        print("PASS initial state and protection reflected in observer snapshot")


def stops(binary_dir, inherited):
    with Case(binary_dir, inherited) as case:
        server = case.server([1, 16, 10, 10, 40, 15, 20, 1, 0, 50])
        monitor = case.client("monitor_app")
        monitor.expect("SNAPSHOT_END")
        server.send("\n")
        final = monitor.expect("END reason=all_dried time=0.100 alive=0")
        for process in (server, monitor):
            process.wait()
        print("PASS all flowers dry with unlimited simulation duration")
        print(final)

    with Case(binary_dir, inherited) as case:
        server = case.server([1, 42, 5, 5, 40, 15, 20, 1, 10, 50], "--gardeners", "2")
        monitor = case.client("monitor_app")
        monitor.expect("SNAPSHOT_END")
        gardener = case.client("gardener_app")
        gardener.expect("WAIT ")
        server.interrupt()
        final = monitor.expect("END reason=signal time=0.000")
        for process in (server, monitor, gardener):
            process.wait()
        assert final in gardener.lines
        print("PASS server interrupt while waiting stops both clients")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--inherited", type=Path, metavar="BINARY_DIR")
    options = parser.parse_args()
    binary_dir = options.inherited.resolve() if options.inherited else ROOT
    inherited = options.inherited is not None
    print("Transport:", "inherited stream sockets" if inherited else "TCP on 127.0.0.1")
    multiple_monitors(binary_dir, inherited)
    replace_gardener(binary_dir, inherited)
    protection_and_overwater(binary_dir, inherited)
    stops(binary_dir, inherited)


if __name__ == "__main__":
    main()
