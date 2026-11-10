#ifndef FLOWER_H
#define FLOWER_H

#include <stdbool.h>

typedef enum {
  FLOWER_WATERED,
  FLOWER_NORMAL,
  FLOWER_WILTING,
  FLOWER_DRYED,
  FLOWER_DEAD_OVERWATER
} FlowerState;

typedef struct {
  FlowerState state;
  double moisture;
  double dry_rate;
  int watering_by; // -1 если никто не поливает
  double remaining_water;
  double watering_rate;
  double protected_until;
  int water_count;
  bool dead;
} Flower;

#endif
