#include "../common/client.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Использование: %s IP ПОРТ\n", argv[0]);
        return EXIT_FAILURE;
    }
    return run_client(argv[1], argv[2], "MONITOR\n", false);
}
