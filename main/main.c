/**
 * CursedArtifact (easy level room).
 *
 * Order goes (1) Compass Ritual (2) Hall Sensor (key) (3) Ultrasonic scales (ma'at) (4) Breathing LED jewels (cursed jewels)
 */
#include "artifact_state.h"
#include "room_net.h"
#include "room_session.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "display_theme.h"
#include "esp_lcd_ili9341.h"
#include "esp32s3_box_lcd_config.h"

static const char *TAG = "cursed_artifact";

lv_disp_t *g_disp = NULL;
adc_oneshot_unit_handle_t g_adc1 = NULL;
adc_channel_t g_hall_ch = ADC_CHANNEL_0;

void artifact_interstitial(const char *title, const char *body, int delay_ms) {
  if (!g_disp) {
    return;
  }
  lvgl_port_lock(0);
  lv_obj_t *scr = lv_disp_get_scr_act(g_disp);
  lv_obj_clean(scr);
  ui_apply_light_screen(scr);
  const int32_t hres = lv_display_get_horizontal_resolution(g_disp);
  const int32_t margin = 10;
  const int32_t max_w = (hres > 2 * margin) ? (hres - 2 * margin) : hres;
  lv_obj_t *h = lv_label_create(scr);
  lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(h, max_w);
  lv_label_set_text(h, title);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(h, lv_color_hex(UI_TEXT_TITLE), 0);
  lv_obj_align(h, LV_ALIGN_TOP_MID, 0, margin);
  lv_obj_t *b = lv_label_create(scr);
  lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(b, max_w);
  lv_label_set_text(b, body);
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(b, lv_color_hex(UI_TEXT_BODY), 0);
  lv_obj_align_to(b, h, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
  lvgl_port_unlock();
  if (delay_ms > 0) {
    artifact_delay_ms((uint32_t)delay_ms);
  }
}

void artifact_set_display_rotation(lv_display_rotation_t rotation) {
  if (!g_disp) {
    return;
  }
  lvgl_port_lock(0);
  lv_display_set_rotation(g_disp, rotation);
  lvgl_port_unlock();
}

static lv_disp_t *gui_setup(void) {
  gpio_config_t bk_gpio_config = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
  };
  ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

  spi_bus_config_t bus_config = {
      .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
      .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
      .miso_io_num = EXAMPLE_PIN_NUM_MISO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
      .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
      .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
      .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
      .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
      .spi_mode = 0,
      .trans_queue_depth = 10,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                           &io_config, &io_handle));

  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
      .flags.reset_active_high = 1,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  lvgl_port_init(&lvgl_cfg);

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = io_handle,
      .panel_handle = panel_handle,
      .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES,
      .double_buffer = true,
      .hres = EXAMPLE_LCD_H_RES,
      .vres = EXAMPLE_LCD_V_RES,
      .monochrome = false,
      .flags = {.swap_bytes = true},
      .rotation = {
          .swap_xy = false,
          .mirror_x = true,
          .mirror_y = true,
      }};
  lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
  ui_apply_light_screen(lv_disp_get_scr_act(disp));
  return disp;
}

/** @return 0 success, 1 reset/timer/abort, 2 jewel minigame failure */
static int run_ritual_chain(void) {
  room_set_phase("compass");
  ESP_LOGI(TAG, "=== Phase 1: Compass ===");
  artifact_set_display_rotation(LV_DISPLAY_ROTATION_270);
  if (phase_compass() != ARTIFACT_OK) {
    return 1;
  }
  artifact_interstitial("Ritual I complete", "Offer the key to the lock…", 2200);
  if (room_should_abort()) {
    return 1;
  }

  ESP_LOGI(TAG, "=== Phase 2: Hall ===");
  room_set_phase("hall");
  artifact_set_display_rotation(LV_DISPLAY_ROTATION_0);
  if (artifact_hall_adc_add_channel() != ESP_OK) {
    artifact_interstitial("INIT FAIL", "Hall ADC channel failed", 8000);
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  if (phase_hall() != ARTIFACT_OK) {
    return 1;
  }
  artifact_interstitial("Ritual II complete", "The scales await judgment…", 2200);
  if (room_should_abort()) {
    return 1;
  }

  ESP_LOGI(TAG, "=== Phase 3: Scales of Ma'at ===");
  room_set_phase("scales");
  artifact_set_display_rotation(LV_DISPLAY_ROTATION_90);
  if (phase_scales() != ARTIFACT_OK) {
    return 1;
  }
  artifact_interstitial("Ritual III complete", "The jewels stir…", 2200);
  if (room_should_abort()) {
    return 1;
  }

  ESP_LOGI(TAG, "=== Phase 4: Jewels ===");
  room_set_phase("jewels");
  artifact_set_display_rotation(LV_DISPLAY_ROTATION_180);
  int jr = phase_jewels();
  if (jr == JEWELS_ABORT) {
    return 1;
  }
  if (jr != JEWELS_OK) {
    return 2;
  }
  return 0;
}

void app_main(void) {
  room_session_init();
  g_disp = gui_setup();
  ESP_LOGI(TAG, "Display OK");
  ESP_ERROR_CHECK(room_net_start());

  for (;;) {
    room_run_begin();
    int rc = run_ritual_chain();
    uint32_t elapsed_ms = room_elapsed_ms();
    bool timed_out = room_timer_expired();
    room_run_finish();

    if (rc == 0) {
      room_on_room_cleared(elapsed_ms);
      artifact_interstitial(
          "ALL RITUALS COMPLETE",
          "The artifact is satisfied.\n\nYOU HAVE\nLIFTED THE\nANCIENT\nCURSE!",
          6000);
    } else if (rc == 1) {
      if (timed_out) {
        artifact_interstitial(
            "TIME'S UP",
            "The five minutes have ended.\n\nPress Reset on the room page to "
            "play again.",
            4000);
      }
    }

    room_set_phase("waiting");
    ESP_LOGI(TAG, "Waiting for Reset (web)…");
    while (!room_reset_requested()) {
      vTaskDelay(pdMS_TO_TICKS(120));
    }
    room_clear_reset();
    room_run_clear_flags();
  }
}
