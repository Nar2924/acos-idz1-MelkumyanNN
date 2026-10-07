#include "gardener.h"
#include "log.h"
#include <unistd.h>

extern volatile int g_stop; // <---- флаг остановки всей программы

void gardener_init(Gardener *gardener, int id, Garden *garden, double speed) {
  gardener->id = id;
  gardener->speed = speed;
  gardener->busy_flower_id = -1;
  gardener->garden = garden;
}

void *gardener_thread(void *arg) {
  Gardener *gardener = arg;
  Garden *garden = gardener->garden;

  while (!g_stop) {
    pthread_mutex_lock(&garden->lock);

    // Ждем, если заняты
    while (!g_stop && gardener->busy_flower_id != -1) {
      int fid = gardener->busy_flower_id;

      // Когда один садовник занят и обрабатывает цветок, его поток не должен
      // блокировать сад, чтобы поток второго садовника и поток самого сада
      // могли также изменять значения цветов в саду, поэтому мы убираем мьютекс
      // и позволяем другим потокам вносить изменеия в сад
      if (garden->flowers[fid].watering_by == gardener->id) {
        pthread_mutex_unlock(&garden->lock);
        usleep(50000); // 50ms проверка
        pthread_mutex_lock(&garden->lock);
        continue;
      } else {
        gardener->busy_flower_id = -1;
      }
    }

    if (g_stop) {
      pthread_mutex_unlock(&garden->lock); //
      break;
    }

    // Закрепляем цветок за садовником и садовника за цветком + логируем событие
    int target = garden_select_flower(garden, gardener->id);
    if (target >= 0) {
      if (garden_start_watering(garden, gardener->id, target)) {
        garden->flowers[target].watering_rate = gardener->speed;
        gardener->busy_flower_id = target;
        log_event(garden->current_time,
                  "GARDENER %d: started watering flower %d", gardener->id,
                  target);
      }
    }

    // Нет цветов, с которыми нужно поработать, ждем, пока сад подаст сигнал
    // через garden->changed
    if (target < 0) {
      pthread_cond_wait(&garden->changed, &garden->lock);
    }
    pthread_mutex_unlock(&garden->lock);
  }
  return NULL;
}
