#include "client.h"
#include "net.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int run_client(const char *ip, const char *port, const char *registration,
               bool gardener) {
  signal(SIGPIPE, SIG_IGN);
  setvbuf(stdout, NULL, _IOLBF, 0);

  int fd = tcp_connect(ip, port);
  if (fd < 0) {
    perror("TCP connect");
    return EXIT_FAILURE;
  }
  if (write_all(fd, registration, strlen(registration)) < 0) {
    perror("TCP registration");
    close(fd);
    return EXIT_FAILURE;
  }
  FILE *input = fdopen(fd, "r");
  if (!input) {
    perror("fdopen");
    close(fd);
    return EXIT_FAILURE;
  }

  char line[512];
  int result = EXIT_FAILURE;
  bool terminal_message = false;
  bool can_request = gardener;
  int selected_flower = -1;
  int selected_cycles = 0;
  double driest = 0.0;
  while (fgets(line, sizeof(line), input)) {
    int flower_id, cycles;
    double moisture;
    if (gardener && sscanf(line, "OPTION flower=%d moisture=%lf cycles=%d",
                           &flower_id, &moisture, &cycles) == 3) {
      if (selected_flower == -1 || moisture < driest) {
        selected_flower = flower_id;
        selected_cycles = cycles;
        driest = moisture;
      }
      continue;
    }
    if (gardener && strcmp(line, "OPTIONS_END\n") == 0) {
      if (can_request && selected_flower != -1) {
        char command[MAX_COMMAND];
        int length = snprintf(command, sizeof(command), "SELECT %d %d\n",
                              selected_flower, selected_cycles);
        printf("SELECT flower=%d moisture=%.3f\n", selected_flower, driest);
        can_request = write_all(fd, command, (size_t)length) == 0;
      }
      selected_flower = -1;
      continue;
    }
    fputs(line, stdout);
    if (strncmp(line, "END ", 4) == 0 || strncmp(line, "ERROR ", 6) == 0) {
      terminal_message = true;
      const char server_error[] = "END reason=server_error ";
      if (strncmp(line, "END ", 4) == 0 &&
          strncmp(line, server_error, sizeof(server_error) - 1) != 0) {
        result = EXIT_SUCCESS;
      }
      break;
    }
    if (can_request &&
        (strncmp(line, "START ", 6) == 0 || strncmp(line, "DONE ", 5) == 0 ||
         strncmp(line, "OVERWATER ", 10) == 0)) {
      can_request = write_all(fd, "READY\n", 6) == 0;
    }
  }
  if (!terminal_message) {
    fputs("TCP-соединение прервалось до получения итогов.\n", stderr);
  }
  fclose(input);
  return result;
}
