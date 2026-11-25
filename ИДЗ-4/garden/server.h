#ifndef SERVER_H
#define SERVER_H

#include "garden_logic.h"
#include <signal.h>

int run_server(int listener, Garden *garden, int start_count, const volatile sig_atomic_t *stop);

#endif
