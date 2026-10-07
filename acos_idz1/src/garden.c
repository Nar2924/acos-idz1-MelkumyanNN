#include "garden.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>

static double random_double(double min, double max) {
  return min + ((double)rand() / RAND_MAX) * (max - min);
}

void garden_init(Garden *garden, const Config *config) {
  garden->flower_count = config->flower_count;
  garden->flowers = malloc(sizeof(Flower) * garden->flower_count);
  garden->current_time = 0.0;
  garden->wilt_threshold = config->wilt_threshold;
  garden->dry_threshold = config->dry_threshold;
  garden->water_volume = config->water_volume;
  garden->protection_interval = config->protection_interval;
  garden->alive_count = garden->flower_count;

  pthread_mutex_init(&garden->lock, NULL);
  pthread_cond_init(&garden->changed, NULL);

  for (int i = 0; i < garden->flower_count; i++) {
    garden->flowers[i].state = FLOWER_NORMAL;
    garden->flowers[i].moisture = config->initial_moisture;
    garden->flowers[i].dry_rate =
        random_double(config->dry_rate_min, config->dry_rate_max);
    garden->flowers[i].watering_by = -1;
    garden->flowers[i].remaining_water = 0.0;
    garden->flowers[i].watering_rate = 0.0;
    garden->flowers[i].protected_until = 0.0;
    garden->flowers[i].water_count = 0;
    garden->flowers[i].dead = false;
  }
}

void garden_destroy(Garden *garden) {
  free(garden->flowers);
  pthread_mutex_destroy(&garden->lock);
  pthread_cond_destroy(&garden->changed);
}

static FlowerState calculate_flower_state(const Garden *garden,
                                          const Flower *flower) {
  if (flower->state == FLOWER_DEAD_OVERWATER)
    return FLOWER_DEAD_OVERWATER;
  if (flower->moisture <= garden->dry_threshold)
    return FLOWER_DRYED;
  if (flower->moisture <= garden->wilt_threshold)
    return FLOWER_WILTING;
  if (garden->current_time < flower->protected_until)
    return FLOWER_WATERED;
  return FLOWER_NORMAL;
}

static const char *state_to_str(FlowerState s) {
  switch (s) {
  case FLOWER_WATERED:
    return "WATERED";
  case FLOWER_NORMAL:
    return "NORMAL";
  case FLOWER_WILTING:
    return "WILTING";
  case FLOWER_DRYED:
    return "DRYED";
  case FLOWER_DEAD_OVERWATER:
    return "DEAD_OVERWATER";
  default:
    return "UNKNOWN";
  }
}

void garden_tick(Garden *garden, double dt) {
  pthread_mutex_lock(
      &garden->lock); // <--- С нашим садом может работать в один момент ровно 1
                      // поток, остальные будут ждать его разблокировки
  garden->current_time += dt;
  bool state_changed = false;

  // Обработка всех цветов
  for (int i = 0; i < garden->flower_count; i++) {
    Flower *f = &garden->flowers[i];
    if (f->dead || f->state == FLOWER_DRYED)
      continue;

    // Цветок никем не поливается
    if (f->watering_by == -1) {
      f->moisture -= f->dry_rate * dt;
      if (f->moisture < 0.0)
        f->moisture = 0.0;
    }
    // Цветок занят садовником
    else {
      double added = f->watering_rate * dt;

      // Полив цветка
      if (added > f->remaining_water)
        added = f->remaining_water;
      f->moisture += added;
      f->remaining_water -= added;

      // Если цветок был полит до конца, то освобождаем его и выводим в консоль
      if (f->remaining_water <= 0.0) {
        f->watering_by = -1;
        f->protected_until = garden->current_time + garden->protection_interval;
        f->water_count++;
        log_event(garden->current_time, "Flower %d watering finished", i);
      }
    }

    // Подсчет состояния цветка и его обновление в случае изменеия
    FlowerState old_state = f->state;
    FlowerState new_state = calculate_flower_state(garden, f);

    if (new_state != old_state) {
      f->state = new_state;
      log_event(garden->current_time, "FLOWER %d: %s -> %s", i,
                state_to_str(old_state), state_to_str(new_state));
      if (new_state == FLOWER_WILTING)
        state_changed = true;
      if (new_state == FLOWER_DRYED || new_state == FLOWER_DEAD_OVERWATER) {
        f->dead = true;
        garden->alive_count--;
      }
    }
  }

  if (state_changed) {
    pthread_cond_broadcast(
        &garden->changed); // <---- Меняем условие, чтобы садовники увидели, что
                           // есть цветы, которые нужно полить
  }
  pthread_mutex_unlock(&garden->lock); // <---- Разблокировка сада
}

// Функция поиска цветка с минимальным уровнем влажности
int garden_select_flower(Garden *garden, int gardener_id) {
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

// Функция полива цветка, которая проверяет, подходит ли нам цветок, и
// закрепляет садовника за цветком и передает цветку информацию о том, сколько
// воды ему прийдет.
bool garden_start_watering(Garden *garden, int gardener_id, int flower_id) {
  if (flower_id < 0 || flower_id >= garden->flower_count)
    return false;
  Flower *f = &garden->flowers[flower_id];

  if (f->dead || f->state == FLOWER_DRYED || f->watering_by != -1)
    return false;
  if (garden->current_time < f->protected_until)
    return false; // Защита

  f->watering_by = gardener_id;
  f->remaining_water = garden->water_volume;
  return true;
}

// Вывод полной статистики по саду
void garden_print_statistics(const Garden *garden) {
  int normal = 0, wilting = 0, watered = 0, dryed = 0, overwater = 0;
  for (int i = 0; i < garden->flower_count; i++) {
    switch (garden->flowers[i].state) {
    case FLOWER_NORMAL:
      normal++;
      break;
    case FLOWER_WILTING:
      wilting++;
      break;
    case FLOWER_WATERED:
      watered++;
      break;
    case FLOWER_DRYED:
      dryed++;
      break;
    case FLOWER_DEAD_OVERWATER:
      overwater++;
      break;
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
