#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Wall-clock limit for a run (matches web countdown). */
#define ROOM_TIME_LIMIT_MS (5U * 60U * 1000U)

#define ROOM_LEADERBOARD_MAX 5
#define ROOM_NAME_MAX_LEN 32

typedef struct {
  char name[ROOM_NAME_MAX_LEN];
  uint32_t time_ms;
} room_lb_entry_t;

void room_session_init(void);

void room_run_begin(void);
void room_run_finish(void);
void room_run_clear_flags(void);

uint32_t room_elapsed_ms(void);
uint32_t room_remaining_ms(void);
bool room_timer_expired(void);

bool room_reset_requested(void);
void room_request_reset(void);
void room_clear_reset(void);

bool room_should_abort(void);

/** After full success (all phases). May set pending name entry for HTTP. */
void room_on_room_cleared(uint32_t elapsed_ms);

bool room_name_entry_pending(void);
uint32_t room_name_entry_time_ms(void);
/** HTTP: submit name for current pending slot; returns ESP_OK or ESP_ERR_INVALID_STATE. */
esp_err_t room_submit_name(const char *name);

size_t room_leaderboard_count(void);
void room_leaderboard_get(size_t index, room_lb_entry_t *out);

/** JSON for /api/state into buf. */
esp_err_t room_state_json(char *buf, size_t buf_len);

const char *room_phase_label(void);
void room_set_phase(const char *label);
