# Cursed Artifact

Firmware for an **ESP32-S3** “escape room box” experience: a multi-phase ritual on an **ILI9341** display (LVGL), with sensors and outputs wired per `main/pin_config.h`. The device exposes a **Wi‑Fi access point** and a small **web UI** so operators can see phase, countdown, reset the run, and record a short leaderboard.

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (v5.x; matches your installed toolchain)
- Board wired for this project (see pins below). The root `CMakeLists.txt` targets an **ESP32-S3-BOX**-style setup with a **240×320** SPI LCD.

## Game flow

Runs are limited to **5 minutes** (`ROOM_TIME_LIMIT_MS` in `main/room_session.h`). Phases run in order:

1. **Compass ritual** — MPU6050 tilt sequence (I2C).
2. **Hall sensor** — analog hall on **ADC1** (e.g. key / magnet).
3. **Scales of Ma’at** — HC-SR04 ultrasonic distance (trigger/echo; use **3.3 V–safe** echo conditioning if needed).
4. **Cursed jewels** — RGB LEDs and matching buttons (no yellow channel: green / red / blue only).

The display rotation changes per phase (see `artifact_set_display_rotation` in `main/artifact_state.h`).

## Wi‑Fi and web UI

On boot, firmware starts a **SoftAP** and HTTP server (`main/room_net.c`):

| Setting | Value |
|--------|--------|
| SSID | `CursedRoom` |
| Password | `escape42` |

Connect a phone or laptop to that network and open the served page (root `/`) for countdown, current phase, **Reset**, and optional name entry after a clear.

## Hardware pins

Authoritative definitions: **`main/pin_config.h`**. Summary:

- **Jewels**: LEDs on GPIO 12 / 42 / 41; buttons on 38 / 21 / 13  
- **LCD (SPI)**: clock 36, MOSI 35, DC 34, CS 33, RST 37, backlight 48  
- **Hall**: ADC GPIO 9  
- **MPU6050**: SDA 40, SCL 11  
- **Ultrasonic**: TRIG 14, ECHO 10  

Change **`pin_config.h`** only if your wiring differs.

## Build and flash

From the project root:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial device (e.g. `/dev/cu.usbmodem*` on macOS).

**Flash layout**: `partitions.csv` allocates a **2 MB** factory app partition; `sdkconfig.defaults` expects **16 MB** flash—align hardware and `sdkconfig` with your module.

## Dependencies (managed components)

Declared in `main/idf_component.yml`, including **LVGL 9**, **esp_lvgl_port**, **esp_lcd_ili9341**, and **mpu6050**.

## Extra folders

- **`jewel_pin_test/`** — small project to exercise jewel GPIOs  
- **`ultrasonic_test/`** — small project to test the ultrasonic setup  

Each has its own `CMakeLists.txt`; build them the same way inside their directories if you use them.
