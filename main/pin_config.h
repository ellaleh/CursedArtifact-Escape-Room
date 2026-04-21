#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

/**
 * Pin configuration for ESP32-S3 Escape Room Box
 */

// ============================================================================
// CURSED JEWELS — RGB only (0=green, 1=red, 2=blue). No yellow channel.
// ============================================================================
#define JEWEL_LED_GREEN_GPIO  12
#define JEWEL_LED_RED_GPIO    42
#define JEWEL_LED_BLUE_GPIO   41
#define JEWEL_BTN_GREEN_GPIO  38
#define JEWEL_BTN_RED_GPIO    21
#define JEWEL_BTN_BLUE_GPIO   13

// ============================================================================
// LCD (ILI9341 SPI)
// ⚠️ Adjust ONLY if your wiring is different
// ============================================================================
#define LCD_CLK   36
#define LCD_MOSI  35
#define LCD_DC    34
#define LCD_CS    33
#define LCD_RST   37
#define LCD_BL    48

#define LCD_H_RES 240
#define LCD_V_RES 320

// ============================================================================
// HALL (analog, e.g. E49) — ADC1
// ============================================================================
#define HALL_ADC_GPIO 9

// ============================================================================
// MPU6050
// ============================================================================
#define MPU6050_SDA_PIN 40
#define MPU6050_SCL_PIN 11

// ============================================================================
// HC-SR04 ultrasonic (Scales of Ma'at) — TRIG out, ECHO in (3.3 V–safe echo!)
// ============================================================================
#define ULTRASONIC_TRIG_GPIO 14
#define ULTRASONIC_ECHO_GPIO 10

#endif // PIN_CONFIG_H