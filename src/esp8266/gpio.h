#pragma once

#include "esp_err.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gpio_reset_pin(gpio_num_t gpio_num);

#ifdef __cplusplus
}
#endif
