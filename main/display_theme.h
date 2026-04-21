/**
 * light and warm LCD palette
 */
#pragma once

#include "lvgl.h"

/** Full-screen background */
#define UI_SCR_BG 0xE8E4DC
/** Headlines / emphasis */
#define UI_TEXT_TITLE 0x3A3028
/** Body / secondary */
#define UI_TEXT_BODY 0x5A4E42
/** Progress bar track */
#define UI_BAR_TRACK 0xC8BCB0
/** Progress bar fill */
#define UI_BAR_FILL 0xB8941E

static inline void ui_apply_light_screen(lv_obj_t *scr) {
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_SCR_BG), 0);
}
