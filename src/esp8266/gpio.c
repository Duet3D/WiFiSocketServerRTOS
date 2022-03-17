#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_bit_defs.h"

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
	assert(gpio_num >= 0 && GPIO_IS_VALID_GPIO(gpio_num));
	gpio_config_t cfg = {
		.pin_bit_mask = BIT(gpio_num),
		.mode = GPIO_MODE_DISABLE,
		.pull_up_en = true,
		.pull_down_en = false,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&cfg);
	return ESP_OK;
}


