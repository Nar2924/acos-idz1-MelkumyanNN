#include "garden_logic.h"
#include "../common/ipc_names.h"
#include "../common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static double random_double(double min, double max) {
  return min + ((double)rand() / RAND_MAX) * (max - min);
}

void garden_init_data(SharedGarden *garden, const Config *config) {
  garden->flower_count = config->flower_count;
  garden->current_time = 0.0;
  garden->wilt_threshold = config->wilt_threshold;
  garden->dry_threshold = config->dry_threshold;
  garden->water_volume = config->water_volume;
  garden->protection_interval = config->protection_interval;
  garden->alive_count = config->flower_count;
  garden->stop_flag = 0;
  
  garden->simulation_started = 0;
  garden->registered_gardeners = 0;

  for (int i = 0; i < garden->flower_count; i++) {
    garden->flowers[i].state = FLOWER_NORMAL;
    garden->flowers[i].moisture = config->initial_moisture;
    garden->flowers[i].dry_rate = random_double(config->dry_rate_min, config->dry_rate_max);
    garden->flowers[i].watering_by = -1;
    garden->flowers[i].remaining_water = 0.0;
    garden->flowers[i].watering_rate = 0.0;
    garden->flowers[i].protected_until = 0.0;
    garden->flowers[i].water_count = 0;
    garden->flowers[i].dead = false;
  }
}

static FlowerState calculate_flower_state(const SharedGarden *garden, const Flower *flower) {
  if (flower->state == FLOWER_DEAD_OVERWATER) return FLOWER_DEAD_OVERWATER;
  if (flower->moisture <= garden->dry_threshold) return FLOWER_DRYED;
  if (flower->moisture <= garden->wilt_threshold) return FLOWER_WILTING;
  if (garden->current_time < flower->protected_until) return FLOWER_WATERED;
  return FLOWER_NORMAL;
}

static const char *state_to_str(FlowerState s) {
  switch (s) {
  case FLOWER_WATERED: return "WATERED";
  case FLOWER_NORMAL: return "NORMAL";
  case FLOWER_WILTING: return "WILTING";
  case FLOWER_DRYED: return "DRYED";
  case FLOWER_DEAD_OVERWATER: return "DEAD_OVERWATER";
  default: return "UNKNOWN";
  }
}

bool garden_tick(SharedGarden *garden, double dt) {
  garden->current_time += dt;
  bool state_changed = false;

  for (int i = 0; i < garden->flower_count; i++) {
    Flower *f = &garden->flowers[i];
    if (f->dead || f->state == FLOWER_DRYED) continue;

    if (f->watering_by == -1) {
      f->moisture -= f->dry_rate * dt;
      if (f->moisture < 0.0) f->moisture = 0.0;
    } else {
      double added = f->watering_rate * dt;
      if (added > f->remaining_water) added = f->remaining_water;
      f->moisture += added;
      f->remaining_water -= added;

      if (f->remaining_water <= 0.0) {
        f->watering_by = -1;
        f->protected_until = garden->current_time + garden->protection_interval;
        f->water_count++;
        log_event(garden->current_time, "Flower %d watering finished", i);
      }
    }

    FlowerState old_state = f->state;
    FlowerState new_state = calculate_flower_state(garden, f);

    if (new_state != old_state) {
      f->state = new_state;
      log_event(garden->current_time, "FLOWER %d: %s -> %s", i, state_to_str(old_state), state_to_str(new_state));
      if (new_state == FLOWER_WILTING) state_changed = true;
      if (new_state == FLOWER_DRYED || new_state == FLOWER_DEAD_OVERWATER) {
        f->dead = true;
        garden->alive_count--;
      }
    }
  }
  return state_changed;
}

void garden_print_statistics(const SharedGarden *garden) {
  int normal = 0, wilting = 0, watered = 0, dryed = 0, overwater = 0;
  for (int i = 0; i < garden->flower_count; i++) {
    switch (garden->flowers[i].state) {
    case FLOWER_NORMAL: normal++; break;
    case FLOWER_WILTING: wilting++; break;
    case FLOWER_WATERED: watered++; break;
    case FLOWER_DRYED: dryed++; break;
    case FLOWER_DEAD_OVERWATER: overwater++; break;
    }
  }
  printf("\n===== SIMULATION STATISTICS =====\n");
  printf("Simulation time: %.2f\n", garden->current_time);
  printf("Total flowers: %d\n", garden->flower_count);
  printf("Watered/Normal: %d / %d\n", watered, normal);
  printf("Wilting: %d\n", wilting);
  printf("Dried: %d\n", dryed);
  printf("Dead from overwater: %d\n", overwater);
  printf("=================================\n");
}
