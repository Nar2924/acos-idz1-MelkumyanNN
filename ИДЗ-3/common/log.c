#include "log.h"
#include "ipc_names.h"
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static sem_t *log_sem = NULL;

void log_init(void) {
    log_sem = sem_open(SEM_LOG_NAME, O_CREAT, 0666, 1);
    if (log_sem == SEM_FAILED) {
        perror("sem_open(log)");
        exit(EXIT_FAILURE);
    }
}

void log_close(void) {
    if (log_sem != NULL) {
        sem_close(log_sem);
    }
}

void log_event(double time, const char *format, ...) {
    if (!log_sem) return;
    
    sem_wait(log_sem);
    
    int fd = open(LOG_FILE_PATH, O_CREAT | O_APPEND | O_WRONLY, 0666);
    if (fd != -1) {
        char buffer[1024];
        int offset = snprintf(buffer, sizeof(buffer), "[%7.2f] ", time);
        
        va_list args;
        va_start(args, format);
        offset += vsnprintf(buffer + offset, sizeof(buffer) - offset, format, args);
        va_end(args);
        
        if (offset < (int)sizeof(buffer) - 1) {
            buffer[offset] = '\n';
            buffer[offset + 1] = '\0';
            offset++;
        }
        
        write(fd, buffer, offset);
        close(fd);
        
        printf("%s", buffer);
    }
    
    sem_post(log_sem);
}
