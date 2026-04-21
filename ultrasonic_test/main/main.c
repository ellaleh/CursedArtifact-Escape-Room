/**
 * HC-SR04 ultrasonic smoke test — same pins and timing as CursedArtifact
 * `phase_scales` / `scales_measure_cm` in main/puzzles.c.
 *
 * Flash this project, open serial monitor (115200). You should see distance
 * in cm when the sensor sees a target; "no echo" means timeout / wiring /
 * voltage divider on ECHO.
 *
 * Pins must match main/pin_config.h:
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ultra_test";

/* --- Keep in sync with CursedArtifact main/pin_config.h --- */
#define ULTRASONIC_TRIG_GPIO 14
#define ULTRASONIC_ECHO_GPIO 10

#define INVALID_CM 999.0f
#define EDGE_TIMEOUT_US 30000
#define MIN_PULSE_US 116.f

static void gpio_init(void) {
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

/** Same algorithm as `scales_measure_cm()` in puzzles.c */
static float measure_cm(void) {
  gpio_set_level(ULTRASONIC_TRIG_GPIO, 0);
  esp_rom_delay_us(2);
  gpio_set_level(ULTRASONIC_TRIG_GPIO, 1);
  esp_rom_delay_us(10);
  gpio_set_level(ULTRASONIC_TRIG_GPIO, 0);

  int64_t t0 = esp_timer_get_time();
  while (gpio_get_level(ULTRASONIC_ECHO_GPIO) == 0) {
    if (esp_timer_get_time() - t0 > EDGE_TIMEOUT_US) {
      return INVALID_CM;
    }
  }
  int64_t t_rise = esp_timer_get_time();
  while (gpio_get_level(ULTRASONIC_ECHO_GPIO) == 1) {
    if (esp_timer_get_time() - t_rise > EDGE_TIMEOUT_US) {
      return INVALID_CM;
    }
  }
  int64_t t_fall = esp_timer_get_time();
  float pulse_us = (float)(t_fall - t_rise);
  if (pulse_us < MIN_PULSE_US) {
    return INVALID_CM;
  }
  return pulse_us * 0.034f / 2.f;
}

void app_main(void) {
  gpio_init();
  ESP_LOGI(TAG,
           "HC-SR04 test — TRIG=GPIO%d ECHO=GPIO%d (200 ms samples). "
           "Point at an object 5–200 cm away.",
           ULTRASONIC_TRIG_GPIO, ULTRASONIC_ECHO_GPIO);

  while (true) {
    float d = measure_cm();
    if (d >= INVALID_CM - 1.f) {
      ESP_LOGW(TAG, "no echo (timeout, short pulse, or no object in range)");
    } else {
      ESP_LOGI(TAG, "distance: %.1f cm", d);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
