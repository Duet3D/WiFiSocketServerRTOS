#pragma once

#if ESP32
#include_next "esp_intr_alloc.h"
#else
#define ESP_INTR_FLAG_IRAM  0
#define ESP_INTR_FLAG_EDGE  0
#endif