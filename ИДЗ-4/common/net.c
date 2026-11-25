#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

bool parse_integer(const char *text, long min, long max, long *value) {
  char *end;
  errno = 0;
  long n = strtol(text, &end, 10);
  if (errno || end == text || *end || n < min || n > max) {
    return false;
  }
  *value = n;
  return true;
}

bool parse_real(const char *text, double *value) {
  char *end;
  errno = 0;
  double n = strtod(text, &end);
  if (errno || end == text || *end || !isfinite(n) || fabs(n) > 1e9) {
    return false;
  }
  *value = n;
  return true;
}

static int open_tcp_socket(const char *ip, const char *port,
                           struct sockaddr_in *address) {
  long number;
  *address = (struct sockaddr_in){.sin_family = AF_INET};
  if (!parse_integer(port, 1, 65535, &number) ||
      inet_pton(AF_INET, ip, &address->sin_addr) != 1) {
    errno = EINVAL;
    return -1;
  }
  address->sin_port = htons((uint16_t)number);
  return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

static int close_on_error(int fd) {
  int error = errno;
  close(fd);
  errno = error;
  return -1;
}

int tcp_open_listener(const char *ip, const char *port) {
  struct sockaddr_in address;
  int fd = open_tcp_socket(ip, port, &address);
  if (fd < 0) {
    return -1;
  }

  int enabled = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0 ||
      bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
      listen(fd, MAX_CONNECTIONS) < 0 || set_nonblocking(fd) < 0) {
    return close_on_error(fd);
  }
  return fd;
}

int tcp_connect(const char *ip, const char *port) {
  struct sockaddr_in address;
  int fd = open_tcp_socket(ip, port, &address);
  if (fd < 0) {
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    return close_on_error(fd);
  }
  return fd;
}

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int write_all(int fd, const char *data, size_t length) {
  while (length) {
    ssize_t count = write(fd, data, length);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return -1;
    }
    data += count;
    length -= (size_t)count;
  }
  return 0;
}
