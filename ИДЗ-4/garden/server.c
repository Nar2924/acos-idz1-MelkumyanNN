#include "server.h"
#include "../common/net.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    OUTPUT_LIMIT = 256 * 1024,
    HANDSHAKE_MS = 5000,
    SHUTDOWN_MS = 1000
};

typedef enum { UNREGISTERED, GARDENER, MONITOR } Role;

typedef struct {
    int fd;
    Role role;
    int64_t accepted_at;
    char command[MAX_COMMAND];
    size_t command_length;
    // TCP can accept only part of a message, so retain unsent bytes between polls.
    char *output;
    size_t sent;
    size_t used;
    size_t capacity;
    bool closing;
    bool broken;
    bool write_closed;
    int gardener_id;
    double speed;
    int flower_id;
    bool ready;
} Connection;

typedef struct {
    Garden *garden;
    Connection clients[MAX_CONNECTIONS];
    FILE *log;
    bool running;
    int64_t started_at;
    uint64_t ticks;
    int gardener_count;
} Server;

static int64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void queue_bytes(Connection *client, const char *data, size_t length) {
    if (client->broken) {
        return;
    }
    size_t pending = client->used - client->sent;
    if (pending + length > OUTPUT_LIMIT) {
        client->broken = true;
        return;
    }
    if (client->used + length > client->capacity && client->sent) {
        memmove(client->output, client->output + client->sent, pending);
        client->sent = 0;
        client->used = pending;
    }
    if (client->used + length > client->capacity) {
        size_t capacity = client->capacity ? client->capacity : 1024;
        while (capacity < client->used + length) {
            capacity *= 2;
        }
        char *output = realloc(client->output, capacity);
        if (!output) {
            client->broken = true;
            return;
        }
        client->output = output;
        client->capacity = capacity;
    }
    memcpy(client->output + client->used, data, length);
    client->used += length;
}

static void queue_line(Connection *client, const char *format, ...) {
    char line[256];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof(line) - 1, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= sizeof(line) - 1) {
        client->broken = true;
        return;
    }
    line[length++] = '\n';
    queue_bytes(client, line, (size_t)length);
}

static void log_line(Server *server, const char *line) {
    fputs(line, stdout);
    if (server->log) {
        fputs(line, server->log);
    }
}

static void event(Server *server, const char *format, ...) {
    char detail[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);

    char line[320];
    int length = snprintf(line, sizeof(line), "EVENT [t=%.3f] %s\n",
                          server->garden->current_time, detail);
    log_line(server, line);
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        Connection *client = &server->clients[i];
        if (client->fd >= 0 && client->role == MONITOR && !client->closing) {
            queue_bytes(client, line, (size_t)length);
        }
    }
}

static Connection *find_gardener(Server *server, int id) {
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        Connection *client = &server->clients[i];
        if (client->fd >= 0 && client->role == GARDENER && client->gardener_id == id) {
            return client;
        }
    }
    return NULL;
}

static void discard_client(Connection *client) {
    close(client->fd);
    free(client->output);
    *client = (Connection){.fd = -1, .flower_id = -1};
}

static void disconnect_client(Server *server, Connection *client) {
    if (client->role == GARDENER) {
        int id = client->gardener_id;
        int flower_id = client->flower_id;
        --server->gardener_count;
        if (flower_id != -1) {
            Flower *flower = &server->garden->flowers[flower_id];
            garden_cancel_watering(flower);
            event(server, "WATER_CANCEL gardener=%d flower=%d moisture=%.3f",
                  id, flower_id, flower->moisture);
        }
        event(server, "GARDENER_LEFT id=%d", id);
    }
    discard_client(client);
}

static void snapshot(const Server *server, Connection *client) {
    const Garden *garden = server->garden;
    queue_line(client, "SNAPSHOT phase=%s time=%.3f flowers=%d alive=%d gardeners=%d",
               server->running ? "running" : "waiting", garden->current_time,
               garden->config.flower_count, garden->alive_count, server->gardener_count);
    for (int i = 0; i < garden->config.flower_count; ++i) {
        const Flower *flower = &garden->flowers[i];
        queue_line(client,
                   "FLOWER id=%d state=%s moisture=%.3f gardener=%d remaining=%.3f protected_until=%.3f watered=%d",
                   i, flower_state_name(flower->state), flower->moisture,
                   flower->watering_by, flower->remaining_water, flower->protected_until,
                   flower->water_count);
    }
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        const Connection *gardener = &server->clients[i];
        if (gardener->fd >= 0 && gardener->role == GARDENER && !gardener->closing) {
            queue_line(client, "GARDENER id=%d speed=%.3g flower=%d",
                       gardener->gardener_id, gardener->speed, gardener->flower_id);
        }
    }
    queue_line(client, "SNAPSHOT_END");
}

static void reject(Connection *client, const char *reason) {
    queue_line(client, "ERROR %s", reason);
    client->closing = true;
}

static void register_client(Server *server, Connection *client) {
    if (strcmp(client->command, "MONITOR") == 0) {
        client->role = MONITOR;
        snapshot(server, client);
        return;
    }

    char role[16], id_text[32], speed_text[64], extra;
    long id;
    double speed;
    if (sscanf(client->command, "%15s %31s %63s %c", role, id_text, speed_text, &extra) != 3 ||
        strcmp(role, "GARDENER") != 0 ||
        !parse_integer(id_text, 0, INT_MAX, &id) ||
        !parse_real(speed_text, &speed) || speed <= 0.0) {
        reject(client, "expected MONITOR or GARDENER <id> <speed>");
        return;
    }

    if (server->gardener_count == MAX_GARDENERS) {
        reject(client, "gardener limit reached");
        return;
    }
    if (id == 0) {
        id = 1;
        while (find_gardener(server, (int)id)) {
            ++id;
        }
    } else if (find_gardener(server, (int)id)) {
        reject(client, "gardener id already in use");
        return;
    }
    client->role = GARDENER;
    client->gardener_id = (int)id;
    client->speed = speed;
    ++server->gardener_count;
    queue_line(client, "CONNECTED gardener=%d speed=%.3g", client->gardener_id, speed);
    if (server->running) {
        queue_line(client, "START time=%.3f", server->garden->current_time);
    } else {
        queue_line(client, "WAIT for simulation start");
    }
    event(server, "GARDENER_JOIN id=%d speed=%.3g", client->gardener_id, speed);
}

static void select_flower(Server *server, Connection *client) {
    char id_text[32], cycles_text[32], extra;
    long id, cycles;
    Garden *garden = server->garden;
    if (sscanf(client->command, "SELECT %31s %31s %c", id_text, cycles_text, &extra) != 2 ||
        !parse_integer(id_text, 0, garden->config.flower_count - 1, &id) ||
        !parse_integer(cycles_text, 0, INT_MAX, &cycles)) {
        reject(client, "expected SELECT <flower> <cycles>");
        return;
    }

    Flower *flower = &garden->flowers[id];
    FlowerState previous = flower->state;
    client->ready = false;
    event(server, "WATER_SELECT gardener=%d flower=%ld", client->gardener_id, id);
    WateringResult result = garden_try_watering(garden, (int)id, (int)cycles,
                                              client->gardener_id, client->speed);
    if (result == WATER_STARTED) {
        client->flower_id = (int)id;
        queue_line(client, "WATERING flower=%ld volume=%.3f", id, garden->config.water_volume);
        event(server, "WATER_START gardener=%d flower=%ld moisture=%.3f speed=%.3g",
              client->gardener_id, id, flower->moisture, client->speed);
    } else if (result == WATER_OVERFLOW) {
        queue_line(client, "OVERWATER flower=%ld", id);
        event(server, "OVERWATER gardener=%d flower=%ld protected_until=%.3f",
              client->gardener_id, id, flower->protected_until);
        event(server, "STATE flower=%ld %s -> %s moisture=%.3f", id,
              flower_state_name(previous), flower_state_name(flower->state), flower->moisture);
    } else {
        queue_line(client, "RETRY flower=%ld", id);
        client->ready = true;
    }
}

static void handle_command(Server *server, Connection *client) {
    if (client->role == UNREGISTERED) {
        register_client(server, client);
        return;
    }
    if (client->role != GARDENER || !server->running || client->flower_id != -1) {
        reject(client, "unexpected command");
        return;
    }
    if (strcmp(client->command, "READY") == 0) {
        client->ready = true;
    } else if (strncmp(client->command, "SELECT ", 7) == 0) {
        select_flower(server, client);
    } else {
        reject(client, "expected READY or SELECT <flower> <cycles>");
    }
}

static void receive_commands(Server *server, Connection *client) {
    // One TCP read can contain a fragment of a command or several commands.
    char data[1024];
    ssize_t count = read(client->fd, data, sizeof(data));
    if (count <= 0) {
        if (count == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
            client->broken = true;
        }
        return;
    }
    for (ssize_t i = 0; i < count && !client->closing && !client->broken; ++i) {
        unsigned char byte = (unsigned char)data[i];
        if (byte == '\n') {
            if (client->command_length && client->command[client->command_length - 1] == '\r') {
                --client->command_length;
            }
            client->command[client->command_length] = '\0';
            handle_command(server, client);
            client->command_length = 0;
        } else if ((byte != '\r' && (byte < ' ' || byte > '~')) ||
                   client->command_length == sizeof(client->command) - 1) {
            reject(client, "invalid command");
        } else {
            client->command[client->command_length++] = (char)byte;
        }
    }
}

static void flush_client(Connection *client) {
    ssize_t count = write(client->fd, client->output + client->sent, client->used - client->sent);
    if (count > 0) {
        client->sent += (size_t)count;
        if (client->sent == client->used) {
            client->sent = 0;
            client->used = 0;
        }
    } else if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        client->broken = true;
    }
}

static void accept_clients(Server *server, int listener) {
    // accept returns a new descriptor; the listener stays open for other clients.
    // Bound accept work per poll so a burst of connections cannot delay the model.
    for (int accepted = 0; accepted < 16; ++accepted) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("accept");
            }
            return;
        }
        int slot = 0;
        while (slot < MAX_CONNECTIONS && server->clients[slot].fd >= 0) {
            ++slot;
        }
        if (slot == MAX_CONNECTIONS || set_nonblocking(fd) < 0) {
            close(fd);
            continue;
        }
        server->clients[slot] = (Connection){
            .fd = fd, .accepted_at = monotonic_ms(), .flower_id = -1
        };
    }
}

static void start_simulation(Server *server, int64_t now) {
    server->running = true;
    server->started_at = now;
    event(server, "START gardeners=%d", server->gardener_count);
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        Connection *client = &server->clients[i];
        if (client->fd >= 0 && client->role == GARDENER && !client->closing) {
            queue_line(client, "START time=0.000");
        }
    }
}

static void notify_change(void *context, GardenEvent kind, int flower_id, int previous) {
    Server *server = context;
    const Flower *flower = &server->garden->flowers[flower_id];
    if (kind == WATERING_FINISHED) {
        int gardener_id = previous;
        Connection *client = find_gardener(server, gardener_id);
        if (client) {
            client->flower_id = -1;
            queue_line(client, "DONE flower=%d moisture=%.3f protected_until=%.3f",
                       flower_id, flower->moisture, flower->protected_until);
        }
        event(server, "WATER_DONE gardener=%d flower=%d moisture=%.3f protected_until=%.3f",
              gardener_id, flower_id, flower->moisture, flower->protected_until);
    } else {
        event(server, "STATE flower=%d %s -> %s moisture=%.3f", flower_id,
              flower_state_name((FlowerState)previous), flower_state_name(flower->state),
              flower->moisture);
    }
}

static void offer_work(Server *server) {
    const Garden *garden = server->garden;
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        Connection *client = &server->clients[i];
        if (client->fd < 0 || !client->ready || client->closing || client->broken) {
            continue;
        }
        bool offered = false;
        for (int flower_id = 0; flower_id < garden->config.flower_count; ++flower_id) {
            const Flower *flower = &garden->flowers[flower_id];
            if (garden_is_available(garden, flower)) {
                queue_line(client, "OPTION flower=%d moisture=%.17g cycles=%d",
                           flower_id, flower->moisture, flower->water_count);
                offered = true;
            }
        }
        if (offered) {
            queue_line(client, "OPTIONS_END");
            client->ready = false;
        }
    }
}

static void finish_connection(Connection *client) {
    if (!client->broken && client->sent < client->used) {
        flush_client(client);
    }
    if (client->broken) {
        discard_client(client);
    } else if (client->sent == client->used) {
        if (!client->write_closed) {
            shutdown(client->fd, SHUT_WR);
            client->write_closed = true;
        }
        // Drain a READY sent in response to the final DONE before closing TCP.
        char buffer[256];
        ssize_t count = read(client->fd, buffer, sizeof(buffer));
        if (count == 0 || (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
            discard_client(client);
        }
    }
}

static void finish_simulation(Server *server, const char *reason) {
    const Garden *garden = server->garden;
    int counts[FLOWER_DEAD_OVERWATER + 1] = {0};
    long long cycles = 0;
    for (int i = 0; i < garden->config.flower_count; ++i) {
        ++counts[garden->flowers[i].state];
        cycles += garden->flowers[i].water_count;
    }
    if (strcmp(reason, "all_dead") == 0 && counts[FLOWER_DEAD_OVERWATER] == 0) {
        reason = "all_dried";
    }

    char end[256];
    int length = snprintf(end, sizeof(end),
                          "END reason=%s time=%.3f alive=%d total=%d normal=%d watered=%d wilting=%d dried=%d overwater=%d cycles=%lld\n",
                          reason, garden->current_time, garden->alive_count, garden->config.flower_count,
                          counts[FLOWER_NORMAL], counts[FLOWER_WATERED], counts[FLOWER_WILTING],
                          counts[FLOWER_DRYED], counts[FLOWER_DEAD_OVERWATER], cycles);
    log_line(server, end);
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        Connection *client = &server->clients[i];
        if (client->fd >= 0 && client->role != UNREGISTERED && !client->closing) {
            queue_bytes(client, end, (size_t)length);
        }
    }

    int64_t deadline = monotonic_ms() + SHUTDOWN_MS;
    while (monotonic_ms() < deadline) {
        bool pending = false;
        for (int i = 0; i < MAX_CONNECTIONS; ++i) {
            Connection *client = &server->clients[i];
            if (client->fd >= 0) {
                finish_connection(client);
                pending |= client->fd >= 0;
            }
        }
        if (!pending) {
            break;
        }
        poll(NULL, 0, 10);
    }
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        if (server->clients[i].fd >= 0) {
            discard_client(&server->clients[i]);
        }
    }
}

static int64_t tick_deadline(const Server *server) {
    double milliseconds = (server->ticks + 1) * (double)server->garden->config.time_step_ms;
    double duration = server->garden->config.duration;
    if (duration > 0.0 && milliseconds > duration * 1000.0) {
        milliseconds = duration * 1000.0;
    }
    int64_t whole_ms = (int64_t)milliseconds;
    return server->started_at + whole_ms + (milliseconds > whole_ms);
}

int run_server(int listener, Garden *garden, int start_count, const volatile sig_atomic_t *stop) {
    Server server = {.garden = garden, .log = fopen("simulation.log", "w")};
    if (server.log) {
        setvbuf(server.log, NULL, _IOLBF, 0);
    } else {
        perror("simulation.log");
    }
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        server.clients[i].fd = -1;
    }

    if (start_count < 0) {
        puts("Подключите садовников и наблюдателей. Нажмите Enter для старта.");
    } else {
        printf("Автостарт после подключения садовников: %d\n", start_count);
    }

    const char *reason = "signal";
    int result = 0;
    while (!*stop) {
        struct pollfd fds[MAX_CONNECTIONS + 2] = {
            {.fd = listener, .events = POLLIN},
            {.fd = !server.running && start_count < 0 ? STDIN_FILENO : -1, .events = POLLIN}
        };
        for (int i = 0; i < MAX_CONNECTIONS; ++i) {
            const Connection *client = &server.clients[i];
            fds[i + 2].fd = client->fd;
            fds[i + 2].events = client->closing ? 0 : POLLIN;
            if (client->sent < client->used) {
                fds[i + 2].events |= POLLOUT;
            }
        }

        int64_t before_poll = monotonic_ms();
        int64_t remaining = server.running ? tick_deadline(&server) - before_poll : 250;
        int timeout = remaining < 0 ? 0 : remaining > 250 ? 250 : (int)remaining;
        if (poll(fds, MAX_CONNECTIONS + 2, timeout) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            reason = "server_error";
            result = -1;
            break;
        }

        int64_t now = monotonic_ms();
        for (int i = 0; i < MAX_CONNECTIONS; ++i) {
            Connection *client = &server.clients[i];
            short events = fds[i + 2].revents;
            if (client->fd < 0) {
                continue;
            }
            if (!client->closing && (events & POLLIN)) {
                receive_commands(&server, client);
            }
            if (!client->broken && (events & POLLOUT) && client->sent < client->used) {
                flush_client(client);
            }
            if (client->role == UNREGISTERED && !client->closing &&
                now - client->accepted_at >= HANDSHAKE_MS) {
                reject(client, "registration timed out");
            }
            if (client->broken || (events & (POLLERR | POLLHUP | POLLNVAL)) ||
                (client->closing && client->sent == client->used)) {
                disconnect_client(&server, client);
            }
        }

        if (*stop) {
            break;
        }
        if (fds[0].revents & POLLIN) {
            accept_clients(&server, listener);
        }

        bool enter = false;
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            char input[256];
            ssize_t length = read(STDIN_FILENO, input, sizeof(input));
            enter = length == 0 || (length > 0 && memchr(input, '\n', (size_t)length));
        }
        if (!server.running && (enter || (start_count >= 0 && server.gardener_count >= start_count))) {
            start_simulation(&server, now);
        }

        if (!server.running) {
            continue;
        }
        if (garden->alive_count == 0) {
            reason = "all_dead";
            break;
        }
        if (now >= tick_deadline(&server)) {
            int step = garden->config.time_step_ms;
            double next_time = (++server.ticks * (double)step) / 1000.0;
            double duration = garden->config.duration;
            bool last_tick = duration > 0.0 && next_time >= duration;
            if (last_tick) {
                next_time = duration;
            }
            garden_tick(garden, next_time - garden->current_time, notify_change, &server);
            if (garden->alive_count == 0 || last_tick) {
                reason = garden->alive_count == 0 ? "all_dead" : "duration";
                break;
            }
        }
        offer_work(&server);
    }

    close(listener);
    finish_simulation(&server, reason);
    if (server.log) {
        fclose(server.log);
    }
    return result;
}
