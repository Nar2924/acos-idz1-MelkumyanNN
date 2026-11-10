#include "gardener_logic.h"

int garden_select_flower(SharedGarden *garden) {
  int best_idx = -1;
  double min_moisture = 999999.0;

  for (int i = 0; i < garden->flower_count; i++) {
    Flower *f = &garden->flowers[i];
    if (!f->dead && f->state == FLOWER_WILTING && f->watering_by == -1 &&
        garden->current_time >= f->protected_until) {
      if (f->moisture < min_moisture) {
        min_moisture = f->moisture;
        best_idx = i;
      }
    }
  }
  return best_idx;
}

bool garden_start_watering(SharedGarden *garden, int gardener_id, int flower_id, double volume) {
  if (flower_id < 0 || flower_id >= garden->flower_count) return false;
  Flower *f = &garden->flowers[flower_id];

  if (f->dead || f->state == FLOWER_DRYED || f->watering_by != -1) return false;
  if (garden->current_time < f->protected_until) return false;

  f->watering_by = gardener_id;
  f->remaining_water = volume;
  return true;
}
