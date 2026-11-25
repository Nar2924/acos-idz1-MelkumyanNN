/* Test transport for workspaces where bind/connect are unavailable. */
#include "../common/net.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static int test_descriptor(const char *name) {
    const char *value = getenv(name);
    if (!value) {
        errno = EINVAL;
        return -1;
    }
    return atoi(value);
}

int tcp_open_listener(const char *ip, const char *port) {
    (void)ip;
    (void)port;
    int fd = test_descriptor("TEST_LISTENER_FD");
    return fd >= 0 && set_nonblocking(fd) == 0 ? fd : -1;
}

int tcp_connect(const char *ip, const char *port) {
    (void)ip;
    (void)port;
    return test_descriptor("TEST_CLIENT_FD");
}

int inherited_accept(int listener, struct sockaddr *address, socklen_t *length) {
    (void)address;
    (void)length;
    int fd;
    ssize_t count = read(listener, &fd, sizeof(fd));
    if (count == sizeof(fd)) {
        return fd;
    }
    if (count >= 0) {
        errno = EAGAIN;
    }
    return -1;
}
