/**
 * Jewel wiring debug — not the escape-room game.
 *
 * Each button (active-high) directly mirrors its paired LED GPIO high/low.
 * Flash this project, open the serial monitor, press one button at a time:
 * the LED that lights should match the button color if wiring matches
 * pin_config.h in the main CursedArtifact project.
 *
 * If the wrong LED lights, swap GPIO numbers below until physical color matches
 * the #define name, then copy those values into CursedArtifact main/pin_config.h.
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "jewel_test";

/* --- Copy from CursedArtifact main/pin_config.h (or edit to match your solder) --- */
#define JEWEL_LED_GREEN_GPIO 12
#define JEWEL_LED_RED_GPIO 42
#define JEWEL_LED_BLUE_GPIO 41
#define JEWEL_BTN_GREEN_GPIO 38
#define JEWEL_BTN_RED_GPIO 21
#define JEWEL_BTN_BLUE_GPIO 13

typedef struct {
  gpio_num_t led;
  gpio_num_t btn;
  const char *name;
} jewel_pair_t;

static const jewel_pair_t pairs[] = {
    {JEWEL_LED_GREEN_GPIO, JEWEL_BTN_GREEN_GPIO, "GREEN"},
    {JEWEL_LED_RED_GPIO, JEWEL_BTN_RED_GPIO, "RED"},
    {JEWEL_LED_BLUE_GPIO, JEWEL_BTN_BLUE_GPIO, "BLUE"},
};

static void init_leds(void) {
  uint64_t mask = 0;
  for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
    mask |= 1ULL << pairs[i].led;
  }
  gpio_config_t io = {
      .pin_bit_mask = mask,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);
  for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
    gpio_set_level(pairs[i].led, 0);
  }
}

static void init_btns(void) {
  uint64_t mask = 0;
  for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
    mask |= 1ULL << pairs[i].btn;
  }
  gpio_config_t io = {
      .pin_bit_mask = mask,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);
}

void app_main(void) {
  init_leds();
  init_btns();

  ESP_LOGI(TAG,
           "Jewel pin test: each button drives its paired LED (active-high). "
           "Press one button — the LED that should match is named in code.");
  ESP_LOGI(TAG,
           "GREEN LED=%d btn=%d | RED LED=%d btn=%d | BLUE LED=%d btn=%d",
           (int)JEWEL_LED_GREEN_GPIO, (int)JEWEL_BTN_GREEN_GPIO,
           (int)JEWEL_LED_RED_GPIO, (int)JEWEL_BTN_RED_GPIO,
           (int)JEWEL_LED_BLUE_GPIO, (int)JEWEL_BTN_BLUE_GPIO);

  static int last_log_btn[3] = {-1, -1, -1};

  while (true) {
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
      int lvl = gpio_get_level(pairs[i].btn);
      gpio_set_level(pairs[i].led, lvl);
      if (lvl != last_log_btn[i]) {
        last_log_btn[i] = lvl;
        if (lvl) {
          ESP_LOGI(TAG, "%s button pressed (GPIO %d) -> LED GPIO %d ON", pairs[i].name,
                   (int)pairs[i].btn, (int)pairs[i].led);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
