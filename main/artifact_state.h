#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#include "lvgl.h"

extern lv_disp_t *g_disp;
extern adc_oneshot_unit_handle_t g_adc1;
extern adc_channel_t g_hall_ch;

#define ARTIFACT_OK 0
#define ARTIFACT_ABORT 1

#define JEWELS_OK 0
#define JEWELS_LOST 1
#define JEWELS_TIMEOUT 2
#define JEWELS_ABORT 3

void artifact_interstitial(const char *title, const char *body, int delay_ms);
void artifact_delay_ms(uint32_t ms);
bool room_should_abort(void);

/** Display: compass 270°, scales 90°, hall 0°, jewels 180°. */
void artifact_set_display_rotation(lv_display_rotation_t rotation);

esp_err_t artifact_hall_adc_add_channel(void);

int phase_compass(void);
int phase_hall(void);
int phase_scales(void);
int phase_jewels(void);
