#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task.h"

extern void setup();
extern void loop();

extern "C" void app_main()
{
	setup();
	vTaskPrioritySet(NULL, ESP_TASK_TCPIP_PRIO + 1);
	while (true) {
		loop ();
	}
}
