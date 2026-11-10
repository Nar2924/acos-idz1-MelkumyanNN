#ifndef GARDENER_H
#define GARDENER_H

#include "garden.h"
#include <pthread.h>

typedef struct {
    int id;
    double speed;
    int busy_flower_id;
    Garden *garden;
    pthread_t thread;
} Gardener;

void gardener_init(Gardener *gardener, int id, Garden *garden, double speed);
void *gardener_thread(void *arg);

#endif
