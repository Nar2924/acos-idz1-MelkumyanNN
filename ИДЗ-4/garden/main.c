#include "server.h"
#include "../common/net.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

static void stop_server(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static int usage(const char *program) {
    fprintf(stderr, "Использование: %s IP ПОРТ [--defaults] [--gardeners N] [--seed N]\n"
                    "                    [--initial-state auto|normal|watered|wilting|dried|overwater]\n",
            program);
    return EXIT_FAILURE;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        return usage(argv[0]);
    }
    long gardener_count = -1;
    long seed = (long)(time(NULL) & INT_MAX);
    bool defaults = false;
    Config config = config_defaults();
    const char *states[] = {"auto", "watered", "normal", "wilting", "dried", "overwater"};
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--defaults") == 0) {
            defaults = true;
        } else if (strcmp(argv[i], "--gardeners") == 0 && i + 1 < argc) {
            if (!parse_integer(argv[++i], 0, MAX_GARDENERS, &gardener_count)) {
                return usage(argv[0]);
            }
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_integer(argv[++i], 0, INT_MAX, &seed)) {
                return usage(argv[0]);
            }
        } else if (strcmp(argv[i], "--initial-state") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            int state = FLOWER_AUTO;
            while (state <= FLOWER_DEAD_OVERWATER && strcmp(name, states[state + 1]) != 0) {
                ++state;
            }
            if (state > FLOWER_DEAD_OVERWATER) {
                return usage(argv[0]);
            }
            config.initial_state = (FlowerState)state;
        } else {
            return usage(argv[0]);
        }
    }

    struct sigaction action = {.sa_handler = stop_server};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    if ((!defaults && config_read(&config) < 0) || config_validate(&config) < 0) {
        return stop_requested ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (stop_requested) {
        return EXIT_SUCCESS;
    }

    srand((unsigned)seed);
    Garden *garden = garden_create(&config);
    if (!garden) {
        perror("garden_create");
        return EXIT_FAILURE;
    }

    int listener = tcp_open_listener(argv[1], argv[2]);
    if (listener < 0) {
        perror("TCP listen");
        free(garden);
        return EXIT_FAILURE;
    }

    printf("LISTEN %s:%s seed=%ld\n", argv[1], argv[2], seed);
    int result = run_server(listener, garden, (int)gardener_count, &stop_requested);
    free(garden);
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
