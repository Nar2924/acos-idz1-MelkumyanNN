#include "log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

static pthread_mutex_t log_lock =
    PTHREAD_MUTEX_INITIALIZER; // статическая инициализация мьютекса

void log_event(double time, const char *format, ...) {
  pthread_mutex_lock(&log_lock);
  printf("[%7.2f] ", time);

  // Функция работает с переменным числом аргументов, поэтому используем va_list
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n");
  pthread_mutex_unlock(&log_lock);
}
