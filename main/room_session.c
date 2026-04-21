#include "room_session.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "room";

#define NVS_NS "room"
#define NVS_KEY_LB "lb_v1"

static int64_t s_run_start_us;
static bool s_run_active;

static volatile bool s_reset_req;

static char s_phase[24] = "boot";

static room_lb_entry_t s_lb[ROOM_LEADERBOARD_MAX];
static size_t s_lb_n;

static uint32_t s_pending_time_ms;
static bool s_pending_name;

static int lb_cmp(const void *a, const void *b) {
  const room_lb_entry_t *x = a;
  const room_lb_entry_t *y = b;
  if (x->time_ms < y->time_ms) {
    return -1;
  }
  if (x->time_ms > y->time_ms) {
    return 1;
  }
  return 0;
}

static void lb_sort(void) {
  if (s_lb_n > 1) {
    qsort(s_lb, s_lb_n, sizeof(s_lb[0]), lb_cmp);
  }
}

static esp_err_t lb_load(void) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    s_lb_n = 0;
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }
  size_t sz = 0;
  err = nvs_get_blob(h, NVS_KEY_LB, NULL, &sz);
  if (err == ESP_ERR_NVS_NOT_FOUND || sz == 0) {
    nvs_close(h);
    s_lb_n = 0;
    return ESP_OK;
  }
  if (sz > sizeof(s_lb)) {
    sz = sizeof(s_lb);
  }
  err = nvs_get_blob(h, NVS_KEY_LB, s_lb, &sz);
  nvs_close(h);
  if (err != ESP_OK) {
    s_lb_n = 0;
    return err;
  }
  s_lb_n = sz / sizeof(s_lb[0]);
  if (s_lb_n > ROOM_LEADERBOARD_MAX) {
    s_lb_n = ROOM_LEADERBOARD_MAX;
  }
  lb_sort();
  return ESP_OK;
}

static esp_err_t lb_save(void) {
  nvs_handle_t h;
  ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
  esp_err_t err =
      nvs_set_blob(h, NVS_KEY_LB, s_lb, s_lb_n * sizeof(s_lb[0]));
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err;
}

void room_session_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  if (lb_load() != ESP_OK) {
    s_lb_n = 0;
  }
  s_run_active = false;
  s_reset_req = false;
  s_pending_name = false;
  s_pending_time_ms = 0;
}

void room_run_begin(void) {
  s_run_start_us = esp_timer_get_time();
  s_run_active = true;
  s_reset_req = false;
}

void room_run_finish(void) {
  s_run_active = false;
}

void room_run_clear_flags(void) {
  s_run_active = false;
  s_reset_req = false;
}

uint32_t room_elapsed_ms(void) {
  if (!s_run_active) {
    return 0;
  }
  int64_t d = esp_timer_get_time() - s_run_start_us;
  if (d < 0) {
    d = 0;
  }
  return (uint32_t)(d / 1000);
}

uint32_t room_remaining_ms(void) {
  if (!s_run_active) {
    return 0;
  }
  uint32_t e = room_elapsed_ms();
  if (e >= ROOM_TIME_LIMIT_MS) {
    return 0;
  }
  return ROOM_TIME_LIMIT_MS - e;
}

bool room_timer_expired(void) {
  if (!s_run_active) {
    return false;
  }
  return room_elapsed_ms() >= ROOM_TIME_LIMIT_MS;
}

bool room_reset_requested(void) {
  return s_reset_req;
}

void room_request_reset(void) {
  s_reset_req = true;
}

void room_clear_reset(void) {
  s_reset_req = false;
}

bool room_should_abort(void) {
  return s_reset_req || room_timer_expired();
}

void room_set_phase(const char *label) {
  snprintf(s_phase, sizeof(s_phase), "%s", label ? label : "?");
}

const char *room_phase_label(void) {
  return s_phase;
}

static bool lb_should_add(uint32_t time_ms) {
  if (s_lb_n < ROOM_LEADERBOARD_MAX) {
    return true;
  }
  return time_ms < s_lb[ROOM_LEADERBOARD_MAX - 1].time_ms;
}

void room_on_room_cleared(uint32_t elapsed_ms) {
  if (!lb_should_add(elapsed_ms)) {
    s_pending_name = false;
    s_pending_time_ms = 0;
    return;
  }
  room_lb_entry_t ne = {0};
  ne.time_ms = elapsed_ms;
  s_lb[s_lb_n++] = ne;
  lb_sort();
  while (s_lb_n > ROOM_LEADERBOARD_MAX) {
    s_lb_n--;
  }
  (void)lb_save();
  s_pending_time_ms = elapsed_ms;
  s_pending_name = true;
  ESP_LOGI(TAG, "Leaderboard slot — enter name on web (time %u ms)", (unsigned)elapsed_ms);
}

bool room_name_entry_pending(void) {
  return s_pending_name;
}

uint32_t room_name_entry_time_ms(void) {
  return s_pending_time_ms;
}

esp_err_t room_submit_name(const char *name) {
  if (!s_pending_name || s_pending_time_ms == 0) {
    return ESP_ERR_INVALID_STATE;
  }
  char buf[ROOM_NAME_MAX_LEN];
  memset(buf, 0, sizeof(buf));
  if (name) {
    strncpy(buf, name, sizeof(buf) - 1);
    for (size_t i = 0; i < sizeof(buf) && buf[i]; i++) {
      if (buf[i] < ' ' || buf[i] > '~') {
        buf[i] = '?';
      }
    }
  }
  if (buf[0] == '\0') {
    strncpy(buf, "Anonymous", sizeof(buf) - 1);
  }
  for (size_t i = 0; i < s_lb_n; i++) {
    if (s_lb[i].time_ms == s_pending_time_ms && s_lb[i].name[0] == '\0') {
      strncpy(s_lb[i].name, buf, sizeof(s_lb[i].name) - 1);
      s_pending_name = false;
      s_pending_time_ms = 0;
      (void)lb_save();
      return ESP_OK;
    }
  }
  s_pending_name = false;
  s_pending_time_ms = 0;
  return ESP_ERR_NOT_FOUND;
}

size_t room_leaderboard_count(void) {
  return s_lb_n;
}

void room_leaderboard_get(size_t index, room_lb_entry_t *out) {
  if (!out || index >= s_lb_n) {
    return;
  }
  *out = s_lb[index];
}

esp_err_t room_state_json(char *buf, size_t buf_len) {
  int n = snprintf(buf, buf_len,
                   "{\"limit_ms\":%u,\"remaining_ms\":%u,\"elapsed_ms\":%u,"
                   "\"running\":%s,\"phase\":\"%s\",\"expired\":%s,"
                   "\"name_pending\":%s,\"pending_time_ms\":%u,\"leaderboard\":[",
                   (unsigned)ROOM_TIME_LIMIT_MS,
                   (unsigned)room_remaining_ms(),
                   (unsigned)room_elapsed_ms(),
                   s_run_active ? "true" : "false", s_phase,
                   room_timer_expired() ? "true" : "false",
                   s_pending_name ? "true" : "false",
                   (unsigned)s_pending_time_ms);
  if (n < 0 || (size_t)n >= buf_len) {
    return ESP_ERR_NO_MEM;
  }
  size_t off = (size_t)n;
  for (size_t i = 0; i < s_lb_n && off + 80 < buf_len; i++) {
    char name_esc[ROOM_NAME_MAX_LEN * 2];
    size_t k = 0;
    for (size_t j = 0; j < sizeof(s_lb[i].name) && s_lb[i].name[j] && k + 2 < sizeof(name_esc); j++) {
      char c = s_lb[i].name[j];
      if (c == '"' || c == '\\') {
        name_esc[k++] = '\\';
      }
      name_esc[k++] = c;
    }
    name_esc[k] = '\0';
    int m = snprintf(buf + off, buf_len - off,
                     "%s{\"name\":\"%s\",\"time_ms\":%u}",
                     i > 0 ? "," : "", name_esc[0] ? name_esc : "—",
                     (unsigned)s_lb[i].time_ms);
    if (m < 0) {
      return ESP_ERR_NO_MEM;
    }
    off += (size_t)m;
  }
  if (off + 8 >= buf_len) {
    return ESP_ERR_NO_MEM;
  }
  snprintf(buf + off, buf_len - off, "]}");
  return ESP_OK;
}

void artifact_delay_ms(uint32_t ms) {
  const uint32_t step = 40;
  while (ms > 0) {
    uint32_t chunk = ms > step ? step : ms;
    vTaskDelay(pdMS_TO_TICKS(chunk));
    ms -= chunk;
    if (room_should_abort()) {
      return;
    }
  }
}
