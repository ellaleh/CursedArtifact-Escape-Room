#pragma once

#include "esp_err.h"

/** Starts SoftAP + HTTP (call after display init; uses room_session). */
esp_err_t room_net_start(void);
