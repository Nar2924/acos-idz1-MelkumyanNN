#ifndef GARDEN_H
#define GARDEN_H

#include "config.h"
#include "flower.h"
#include <pthread.h>

typedef struct {
  Flower *flowers;
  int flower_count;
  double current_time;
  double wilt_threshold;
  double dry_threshold;
  double water_volume;
  double protection_interval;
  int alive_count;
  pthread_mutex_t lock;
  pthread_cond_t changed;
} Garden;

void garden_init(Garden *garden, const Config *config);
void garden_destroy(Garden *garden);
void garden_tick(Garden *garden, double dt);
int garden_select_flower(Garden *garden, int gardener_id);
bool garden_start_watering(Garden *garden, int gardener_id, int flower_id);
void garden_print_statistics(const Garden *garden);

#endif
