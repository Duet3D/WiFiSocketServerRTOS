#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task.h"

#include "Config.h"

extern void setup();
extern void loop();

extern "C" void app_main()
{
	vTaskPrioritySet(NULL, MAIN_PRIO);
	setup();
	while (true) {
		loop ();
	}
}
