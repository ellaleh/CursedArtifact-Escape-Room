/**
 * cover the puzzle phases (subrooms)
 */
#include "artifact_state.h"
#include "display_theme.h"
#include "pin_config.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mpu6050.h"
#include "esp_rom_sys.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "puzzles";

/* -------------------------------------------------------------------------- */
/* ADC1 — hall only  */
/* -------------------------------------------------------------------------- */

esp_err_t artifact_hall_adc_add_channel(void) {
  if (g_adc1 == NULL) {
    adc_oneshot_unit_init_cfg_t init = {.unit_id = ADC_UNIT_1};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init, &g_adc1), TAG,
                        "adc1 new_unit");
    ESP_LOGI(TAG, "ADC1 unit ready");
  }
  adc_unit_t unit = ADC_UNIT_1;
  ESP_RETURN_ON_ERROR(
      adc_oneshot_io_to_channel(HALL_ADC_GPIO, &unit, &g_hall_ch), TAG,
      "hall io_to_channel");
  if (unit != ADC_UNIT_1) {
    ESP_LOGE(TAG, "Hall must use ADC1");
    return ESP_ERR_INVALID_ARG;
  }
  adc_oneshot_chan_cfg_t chan_cfg = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_12,
  };
  return adc_oneshot_config_channel(g_adc1, g_hall_ch, &chan_cfg);
}

/* -------------------------------------------------------------------------- */
/* Phase 1: Compass ritual using MPU6050 */
/* -------------------------------------------------------------------------- */

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define COMPASS_WINDOW 10
#define COMPASS_HOLD_MS 1500
#define COMPASS_TILT_TH 0.5f
#define COMPASS_SEQ_LEN 4

typedef enum {
  ORI_LEFT = 0,
  ORI_RIGHT,
  ORI_FORWARD,
  ORI_BACK,
  ORI_NONE,
} orientation_t;

static const char *ori_names[] = {"TILT LEFT", "TILT RIGHT", "TILT FORWARD",
                                  "TILT BACK", "CONTINUE SEARCHING"};

static void i2c_init(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = (gpio_num_t)MPU6050_SDA_PIN,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_io_num = (gpio_num_t)MPU6050_SCL_PIN,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
      .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
}

static orientation_t classify_ori(float ax, float ay) {
  bool left = ax < -COMPASS_TILT_TH;
  bool right = ax > COMPASS_TILT_TH;
  bool forward = ay > COMPASS_TILT_TH;
  bool back = ay < -COMPASS_TILT_TH;
  int c = (int)left + (int)right + (int)forward + (int)back;
  if (c != 1) {
    return ORI_NONE;
  }
  if (left) {
    return ORI_LEFT;
  }
  if (right) {
    return ORI_RIGHT;
  }
  if (forward) {
    return ORI_FORWARD;
  }
  return ORI_BACK;
}

static void compass_ui(float ax, float ay, int step, orientation_t cur, int pct,
                       bool complete, orientation_t *seq) {
  lvgl_port_lock(0);
  lv_obj_t *scr = lv_disp_get_scr_act(g_disp);
  lv_obj_clean(scr);
  ui_apply_light_screen(scr);
  if (complete) {
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, "COMPASS RITUAL\nCOMPLETE");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_TEXT_TITLE), 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();
    return;
  }
  int ds = (step < COMPASS_SEQ_LEN) ? step : COMPASS_SEQ_LEN - 1;
  lv_obj_t *s = lv_label_create(scr);
  char b[40];
  snprintf(b, sizeof(b), "Step %d / %d", ds + 1, COMPASS_SEQ_LEN);
  lv_label_set_text(s, b);
  lv_obj_set_style_text_color(s, lv_color_hex(UI_TEXT_TITLE), 0);
  lv_obj_align(s, LV_ALIGN_TOP_LEFT, 5, 5);
  lv_obj_t *p = lv_label_create(scr);
  snprintf(b, sizeof(b), "Hold: %s", ori_names[seq[ds]]);
  lv_label_set_text(p, b);
  lv_obj_set_style_text_color(p, lv_color_hex(UI_TEXT_TITLE), 0);
  lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_t *live = lv_label_create(scr);
  snprintf(b, sizeof(b), "Now: %s", ori_names[cur]);
  lv_label_set_text(live, b);
  lv_obj_set_style_text_color(live, lv_color_hex(UI_TEXT_BODY), 0);
  lv_obj_align(live, LV_ALIGN_CENTER, 0, -16);
  lv_obj_t *axl = lv_label_create(scr);
  snprintf(b, sizeof(b), "ax:%.2f ay:%.2f", ax, ay);
  lv_label_set_text(axl, b);
  lv_obj_set_style_text_color(axl, lv_color_hex(UI_TEXT_BODY), 0);
  lv_obj_align(axl, LV_ALIGN_CENTER, 0, 8);
  lv_obj_t *bar = lv_bar_create(scr);
  lv_obj_set_size(bar, 200, 18);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_bg_color(bar, lv_color_hex(UI_BAR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(UI_BAR_FILL), LV_PART_INDICATOR);
  lv_bar_set_value(bar, pct, LV_ANIM_OFF);
  lvgl_port_unlock();
}

int phase_compass(void) {
  i2c_init();
  mpu6050_handle_t mpu = mpu6050_create(I2C_MASTER_NUM, 0x68u);
  ESP_ERROR_CHECK(mpu6050_config(mpu, ACCE_FS_4G, GYRO_FS_500DPS));
  ESP_ERROR_CHECK(mpu6050_wake_up(mpu));

  orientation_t seq[COMPASS_SEQ_LEN];
  orientation_t last = ORI_NONE;
  for (int i = 0; i < COMPASS_SEQ_LEN; i++) {
    orientation_t pick;
    do {
      pick = (orientation_t)(esp_random() % 4);
    } while (pick == last);
    seq[i] = pick;
    last = pick;
  }

  float bx[COMPASS_WINDOW] = {0};
  float by[COMPASS_WINDOW] = {0};
  int bi = 0;
  int step = 0;
  bool complete = false;
  orientation_t hold_ori = ORI_NONE;
  int64_t hold_start_us = 0;

  while (!complete) {
    if (room_should_abort()) {
      i2c_driver_delete(I2C_MASTER_NUM);
      return ARTIFACT_ABORT;
    }
    mpu6050_acce_value_t acce;
    if (mpu6050_get_acce(mpu, &acce) != ESP_OK) {
      artifact_delay_ms(30);
      continue;
    }
    bx[bi] = acce.acce_x;
    by[bi] = acce.acce_y;
    bi = (bi + 1) % COMPASS_WINDOW;
    float sx = 0, sy = 0;
    for (int i = 0; i < COMPASS_WINDOW; i++) {
      sx += bx[i];
      sy += by[i];
    }
    sx /= COMPASS_WINDOW;
    sy /= COMPASS_WINDOW;
    orientation_t cur = classify_ori(sx, sy);
    int pct = 0;

    if (cur == seq[step]) {
      if (hold_ori != seq[step]) {
        hold_ori = cur;
        hold_start_us = esp_timer_get_time();
      }
      int64_t ems = (esp_timer_get_time() - hold_start_us) / 1000;
      pct = (int)((ems * 100) / COMPASS_HOLD_MS);
      if (pct > 100) {
        pct = 100;
      }
      if (ems >= COMPASS_HOLD_MS) {
        step++;
        hold_ori = ORI_NONE;
        hold_start_us = 0;
        if (step >= COMPASS_SEQ_LEN) {
          complete = true;
        }
      }
    } else {
      hold_ori = ORI_NONE;
      hold_start_us = 0;
    }

    compass_ui(sx, sy, step, cur, pct, complete, seq);
    artifact_delay_ms(25);
  }

  artifact_delay_ms(1800);
  if (room_should_abort()) {
    i2c_driver_delete(I2C_MASTER_NUM);
    return ARTIFACT_ABORT;
  }
  i2c_driver_delete(I2C_MASTER_NUM);
  return ARTIFACT_OK;
}

/* -------------------------------------------------------------------------- */
/* Phase 2 — Hall Sensor used to "find" a hidden lock/key */
/* -------------------------------------------------------------------------- */

#define HALL_HOLD_MS 3000
#define HALL_QUIESCENT 2000
#define HALL_THRESHOLD 500

static bool hall_detected(void) {
  int raw = 0;
  if (adc_oneshot_read(g_adc1, g_hall_ch, &raw) != ESP_OK) {
    return false;
  }
  int d = abs(raw - HALL_QUIESCENT);
  return d > HALL_THRESHOLD;
}

static void hall_ui(int pct, bool active, bool done) {
  lvgl_port_lock(0);
  lv_obj_t *scr = lv_disp_get_scr_act(g_disp);
  lv_obj_clean(scr);
  ui_apply_light_screen(scr);
  if (done) {
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, "SEAL LOCKED");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_TEXT_TITLE), 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
  } else {
    lv_obj_t *p = lv_label_create(scr);
    lv_label_set_text(p, "Hold the key to the lock");
    lv_obj_set_style_text_color(p, lv_color_hex(UI_TEXT_TITLE), 0);
    lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_t *st = lv_label_create(scr);
    lv_label_set_text(st, active ? "SENSING…" : "KEY MISSING");
    lv_obj_set_style_text_color(st, lv_color_hex(UI_TEXT_BODY), 0);
    lv_obj_align(st, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 220, 20);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_BAR_FILL), LV_PART_INDICATOR);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
  }
  lvgl_port_unlock();
}

int phase_hall(void) {
  int64_t h0 = 0;
  bool finished = false;
  while (!finished) {
    if (room_should_abort()) {
      return ARTIFACT_ABORT;
    }
    bool on = hall_detected();
    int pct = 0;
    if (on) {
      if (h0 == 0) {
        h0 = esp_timer_get_time();
      }
      int64_t ms = (esp_timer_get_time() - h0) / 1000;
      pct = (int)((ms * 100) / HALL_HOLD_MS);
      if (ms >= HALL_HOLD_MS) {
        finished = true;
      }
    } else {
      h0 = 0;
    }
    hall_ui(pct, on, finished);
    artifact_delay_ms(30);
  }
  artifact_delay_ms(1500);
  if (room_should_abort()) {
    return ARTIFACT_ABORT;
  }
  return ARTIFACT_OK;
}

/* -------------------------------------------------------------------------- */
/* Phase 3: Scales of Ma'at using HC-SR04 ultrasonic sensor  */
/* -------------------------------------------------------------------------- */

#define SCALES_TARGET_CM 10.0f
#define SCALES_TOLERANCE_CM 2.0f
#define SCALES_HOLD_MS 3000
#define SCALES_MIN_CM 5.0f
#define SCALES_MAX_CM 20.0f
#define SCALES_POLL_MS 100
#define SCALES_INVALID_CM 999.0f

typedef enum {
  SCALES_ST_IDLE,
  SCALES_ST_MEASURING,
  SCALES_ST_HOLDING,
} scales_state_t;

static void scales_gpio_init(void) {
  gpio_config_t io = {
      .pin_bit_mask = (1ULL << ULTRASONIC_TRIG_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&io));
  io.pin_bit_mask = (1ULL << ULTRASONIC_ECHO_GPIO);
  io.mode = GPIO_MODE_INPUT;
  ESP_ERROR_CHECK(gpio_config(&io));
  gpio_set_level(ULTRASONIC_TRIG_GPIO, 0);
}

static float scales_measure_cm(void) {
  gpio_set_level(ULTRASONIC_TRIG_GPIO, 0);
  esp_rom_delay_us(2);
  gpio_set_level(ULTRASONIC_TRIG_GPIO, 1);
  esp_rom_delay_us(10);
  gpio_set_level(ULTRASONIC_TRIG_GPIO, 0);

  const int64_t edge_timeout_us = 30000;
  int64_t t0 = esp_timer_get_time();
  while (gpio_get_level(ULTRASONIC_ECHO_GPIO) == 0) {
    if (esp_timer_get_time() - t0 > edge_timeout_us) {
      return SCALES_INVALID_CM;
    }
  }
  int64_t t_rise = esp_timer_get_time();
  while (gpio_get_level(ULTRASONIC_ECHO_GPIO) == 1) {
    if (esp_timer_get_time() - t_rise > edge_timeout_us) {
      return SCALES_INVALID_CM;
    }
  }
  int64_t t_fall = esp_timer_get_time();
  float pulse_us = (float)(t_fall - t_rise);
  if (pulse_us < 116.f) {
    return SCALES_INVALID_CM;
  }
  return pulse_us * 0.034f / 2.f;
}

static bool scales_is_balanced(float d) {
  if (d >= SCALES_INVALID_CM - 1.f) {
    return false;
  }
  return d >= (SCALES_TARGET_CM - SCALES_TOLERANCE_CM) &&
         d <= (SCALES_TARGET_CM + SCALES_TOLERANCE_CM);
}

static bool scales_in_band(float d) {
  if (d >= SCALES_INVALID_CM - 1.f) {
    return false;
  }
  return d > SCALES_MIN_CM && d < SCALES_MAX_CM;
}

static void scales_ui(scales_state_t st, float d_cm, int hold_pct, bool success) {
  lvgl_port_lock(0);
  lv_obj_t *scr = lv_disp_get_scr_act(g_disp);
  lv_obj_clean(scr);
  const int32_t hres = lv_display_get_horizontal_resolution(g_disp);
  const int32_t margin = 10;
  const int32_t mw = (hres > 2 * margin) ? (hres - 2 * margin) : hres;

  if (success) {
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xE8D090), 0);
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "WORTHY!");
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_TEXT_TITLE), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *s = lv_label_create(scr);
    lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s, mw);
    lv_label_set_text(s,
                      "Your heart is lighter\nthan the feather of Ma'at");
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s, lv_color_hex(UI_TEXT_TITLE), 0);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 20);
    lvgl_port_unlock();
    return;
  }

  ui_apply_light_screen(scr);
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(title, mw);
  if (st == SCALES_ST_IDLE) {
    lv_label_set_text(title, "THE SCALES OF MA'AT");
  } else if (st == SCALES_ST_MEASURING) {
    lv_label_set_text(title, "FINDING BALANCE");
  } else {
    lv_label_set_text(title, "BALANCED!");
  }
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT_TITLE), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, margin);

  if (st == SCALES_ST_IDLE) {
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(sub, mw);
    lv_label_set_text(
        sub,
        "Place your hand above the sensor.\n\nStay between 5 and 20 cm, "
        "then hold ~10 cm ± 2 cm for 3 seconds.");
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(UI_TEXT_BODY), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 8);
  } else {
    char buf[72];
    if (d_cm >= SCALES_INVALID_CM - 1.f) {
      snprintf(buf, sizeof(buf), "Distance: — (no echo)");
    } else {
      snprintf(buf, sizeof(buf), "Distance: %.1f cm", d_cm);
    }
    lv_obj_t *dist = lv_label_create(scr);
    lv_label_set_text(dist, buf);
    lv_obj_set_style_text_align(dist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(dist, lv_color_hex(UI_TEXT_BODY), 0);
    lv_obj_align(dist, LV_ALIGN_CENTER, 0, -28);

    lv_obj_t *pos_bar = lv_bar_create(scr);
    lv_obj_set_size(pos_bar, 220, 20);
    lv_bar_set_range(pos_bar, 0, 100);
    int pos = 0;
    if (d_cm < SCALES_INVALID_CM - 1.f) {
      float span = SCALES_MAX_CM - SCALES_MIN_CM;
      pos = (int)(((d_cm - SCALES_MIN_CM) / span) * 100.f);
      if (pos < 0) {
        pos = 0;
      }
      if (pos > 100) {
        pos = 100;
      }
    }
    lv_bar_set_value(pos_bar, pos, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(pos_bar, lv_color_hex(UI_BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pos_bar, lv_color_hex(UI_BAR_FILL), LV_PART_INDICATOR);
    lv_obj_align(pos_bar, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(hint, mw);
    if (st == SCALES_ST_HOLDING) {
      lv_obj_t *hb = lv_bar_create(scr);
      lv_obj_set_size(hb, 240, 26);
      lv_bar_set_range(hb, 0, 100);
      int hp = hold_pct > 100 ? 100 : hold_pct;
      lv_bar_set_value(hb, hp, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(hb, lv_color_hex(UI_BAR_TRACK), LV_PART_MAIN);
      lv_obj_set_style_bg_color(hb, lv_color_hex(0xC9A227), LV_PART_INDICATOR);
      lv_obj_align(hb, LV_ALIGN_BOTTOM_MID, 0, -20);
      snprintf(buf, sizeof(buf), "Hold steady… %d%%", hp);
      lv_label_set_text(hint, buf);
    } else {
      lv_label_set_text(hint, "Adjust height until the bar centers on ~10 cm");
    }
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_BODY), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -52);
  }
  lvgl_port_unlock();
}

int phase_scales(void) {
  scales_gpio_init();
  scales_state_t st = SCALES_ST_IDLE;
  int64_t hold_start_us = 0;
  bool solved = false;

  scales_ui(SCALES_ST_IDLE, 0.f, 0, false);

  while (!solved) {
    if (room_should_abort()) {
      return ARTIFACT_ABORT;
    }
    float d = scales_measure_cm();
    int hold_pct = 0;

    if (st == SCALES_ST_IDLE) {
      if (scales_in_band(d)) {
        st = SCALES_ST_MEASURING;
      }
    } else if (st == SCALES_ST_MEASURING) {
      if (scales_is_balanced(d)) {
        st = SCALES_ST_HOLDING;
        hold_start_us = esp_timer_get_time();
      } else if (!scales_in_band(d)) {
        st = SCALES_ST_IDLE;
      }
    } else if (st == SCALES_ST_HOLDING) {
      if (scales_is_balanced(d)) {
        int64_t held_ms = (esp_timer_get_time() - hold_start_us) / 1000;
        hold_pct = (int)((held_ms * 100) / SCALES_HOLD_MS);
        if (hold_pct > 100) {
          hold_pct = 100;
        }
        if (held_ms >= SCALES_HOLD_MS) {
          scales_ui(SCALES_ST_HOLDING, d, 100, true);
          solved = true;
        }
      } else {
        st = SCALES_ST_MEASURING;
      }
    }

    if (!solved) {
      scales_ui(st, d, hold_pct, false);
    }
    artifact_delay_ms(SCALES_POLL_MS);
  }

  ESP_LOGI(TAG, "Scales of Ma'at — worthy");
  artifact_delay_ms(2200);
  if (room_should_abort()) {
    return ARTIFACT_ABORT;
  }
  return ARTIFACT_OK;
}

/* -------------------------------------------------------------------------- */
/* Phase 4: Breathing LEDs with one randomly chosen to be the "cursed" jewel */
/* -------------------------------------------------------------------------- */

#define J_NORMAL_MS 3000
#define J_CURSED_MS 800
#define J_MAX_BRI 255
#define J_FRAME_MS 16
#define J_DEBOUNCE_MS 60
#define J_MAX_WRONG 3
#define J_TIMEOUT_MS (60 * 1000)
#define NUM_J 3

static const gpio_num_t J_LED[NUM_J] = {
    JEWEL_LED_GREEN_GPIO,
    JEWEL_LED_RED_GPIO,
    JEWEL_LED_BLUE_GPIO,
};
static const gpio_num_t J_BTN[NUM_J] = {
    JEWEL_BTN_GREEN_GPIO,
    JEWEL_BTN_RED_GPIO,
    JEWEL_BTN_BLUE_GPIO,
};
static const ledc_channel_t J_CH[NUM_J] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1,
                                            LEDC_CHANNEL_5};
static const ledc_timer_t J_TM[NUM_J] = {LEDC_TIMER_0, LEDC_TIMER_0,
                                        LEDC_TIMER_1};
static const uint16_t J_GAIN[NUM_J] = {256, 384, 512};
static const bool J_INV[NUM_J] = {false, false, false};

static void j_msg(const char *h, const char *b) {
  lvgl_port_lock(0);
  lv_obj_t *scr = lv_disp_get_scr_act(g_disp);
  lv_obj_clean(scr);
  ui_apply_light_screen(scr);
  const int32_t hres = lv_display_get_horizontal_resolution(g_disp);
  const int32_t margin = 10;
  const int32_t mw = (hres > 2 * margin) ? (hres - 2 * margin) : hres;
  lv_obj_t *lh = lv_label_create(scr);
  lv_label_set_long_mode(lh, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(lh, mw);
  lv_label_set_text(lh, h);
  lv_obj_set_style_text_align(lh, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lh, lv_color_hex(UI_TEXT_TITLE), 0);
  lv_obj_align(lh, LV_ALIGN_TOP_MID, 0, margin);
  lv_obj_t *lb = lv_label_create(scr);
  lv_label_set_long_mode(lb, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(lb, mw);
  lv_label_set_text(lb, b);
  lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lb, lv_color_hex(UI_TEXT_BODY), 0);
  lv_obj_align_to(lb, lh, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
  lvgl_port_unlock();
}

static void j_led_set(int i, uint8_t v) {
  uint32_t x = ((uint32_t)v * (uint32_t)J_GAIN[i]) >> 8;
  if (x > 255u) {
    x = 255u;
  }
  if (J_INV[i]) {
    x = 255u - x;
  }
  ledc_set_duty(LEDC_LOW_SPEED_MODE, J_CH[i], x);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, J_CH[i]);
}

static void j_leds_off(void) {
  for (int i = 0; i < NUM_J; i++) {
    j_led_set(i, 0);
  }
}

static void j_leds_all(uint8_t v) {
  for (int i = 0; i < NUM_J; i++) {
    j_led_set(i, v);
  }
}

static void j_ledc_init(void) {
  ledc_timer_config_t tc = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = 1000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&tc));
  tc.timer_num = LEDC_TIMER_1;
  ESP_ERROR_CHECK(ledc_timer_config(&tc));
  for (int i = 0; i < NUM_J; i++) {
    ledc_channel_config_t ch = {
        .gpio_num = J_LED[i],
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = J_CH[i],
        .timer_sel = J_TM[i],
        .duty = 0,
        .hpoint = 0,
        .intr_type = LEDC_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
  }
  j_leds_off();
}

static void j_btn_init(void) {
  uint64_t m = 0;
  for (int i = 0; i < NUM_J; i++) {
    m |= 1ULL << (uint64_t)J_BTN[i];
  }
  gpio_config_t cfg = {
      .pin_bit_mask = m,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
}

static int j_btn_edge(void) {
  static bool last[NUM_J] = {false, false, false};
  static int64_t lp[NUM_J] = {0, 0, 0};
  int64_t now = esp_timer_get_time();
  int64_t db = (int64_t)J_DEBOUNCE_MS * 1000;
  for (int i = 0; i < NUM_J; i++) {
    int lv = gpio_get_level(J_BTN[i]);
    if (lv == 1 && !last[i]) {
      if (now - lp[i] >= db) {
        lp[i] = now;
        last[i] = true;
        return i;
      }
    } else if (lv == 0) {
      last[i] = false;
    }
  }
  return -1;
}

static uint8_t j_breath(uint32_t t, uint32_t per) {
  const float PI = 3.14159265f;
  float ph = (float)(t % per) / (float)per;
  float s = (sinf(2.0f * PI * ph - PI / 2.0f) + 1.0f) / 2.0f;
  return (uint8_t)(s * (float)J_MAX_BRI);
}

int phase_jewels(void) {
  j_ledc_init();
  j_btn_init();
  int cursed = (int)(esp_random() % NUM_J);
  uint32_t off[NUM_J];
  uint32_t np = esp_random() % J_NORMAL_MS;
  uint32_t cp = esp_random() % J_CURSED_MS;
  for (int i = 0; i < NUM_J; i++) {
    off[i] = (i == cursed) ? cp : np;
  }

  j_msg("CURSED JEWELS", "One jewel lies. Find it.");
  artifact_delay_ms(1200);
  if (room_should_abort()) {
    j_leds_off();
    return JEWELS_ABORT;
  }

  bool solved = false;
  int wrong = 0;
  int64_t t0 = esp_timer_get_time();

  while (true) {
    if (room_should_abort()) {
      j_leds_off();
      return JEWELS_ABORT;
    }
    int64_t now = esp_timer_get_time();
    uint32_t ems = (uint32_t)((now - t0) / 1000);
    if (ems >= J_TIMEOUT_MS) {
      j_msg("TIME'S UP", "The curse remains.");
      j_leds_off();
      return JEWELS_TIMEOUT;
    }
    for (int i = 0; i < NUM_J; i++) {
      uint32_t per = (i == cursed) ? J_CURSED_MS : J_NORMAL_MS;
      j_led_set(i, j_breath(ems + off[i], per));
    }
    int pr = j_btn_edge();
    if (pr >= 0) {
      if (!solved) {
        if (pr == cursed) {
          solved = true;
          j_msg("CURSE BROKEN", "The haunted jewel sleeps.");
          artifact_delay_ms(2800);
          j_leds_off();
          return JEWELS_OK;
        }
        wrong++;
        j_leds_all(J_MAX_BRI);
        artifact_delay_ms(200);
        j_leds_off();
        artifact_delay_ms(100);
        if (wrong >= J_MAX_WRONG) {
          j_msg("LOST", "The artifact wins.");
          j_leds_off();
          return JEWELS_LOST;
        }
      }
    }
    artifact_delay_ms(J_FRAME_MS);
  }
}
