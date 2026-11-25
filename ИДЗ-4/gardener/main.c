#include "../common/client.h"
#include "../common/net.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    long id = 0;
    double speed = 15.0;
    if (argc < 3 || argc > 5 ||
        (argc >= 4 && !parse_integer(argv[3], 0, INT_MAX, &id)) ||
        (argc == 5 && (!parse_real(argv[4], &speed) || speed <= 0.0))) {
        fprintf(stderr, "Использование: %s IP ПОРТ [ID [СКОРОСТЬ]]\n"
                        "ID 0 назначается сервером, скорость по умолчанию 15.\n", argv[0]);
        return EXIT_FAILURE;
    }

    char registration[MAX_COMMAND];
    snprintf(registration, sizeof(registration), "GARDENER %ld %.17g\n", id, speed);
    return run_client(argv[1], argv[2], registration, true);
}
