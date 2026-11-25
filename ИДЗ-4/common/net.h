#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_COMMAND 128
#define MAX_GARDENERS 2
#define MAX_CONNECTIONS 128

bool parse_integer(const char *text, long min, long max, long *value);
bool parse_real(const char *text, double *value);
int tcp_open_listener(const char *ip, const char *port);
int tcp_connect(const char *ip, const char *port);
int set_nonblocking(int fd);
int write_all(int fd, const char *data, size_t length);

#endif
