#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task.h"

extern void setup();
extern void loop();

extern "C" void app_main()
{
    vTaskPrioritySet(NULL, ESP_TASK_PRIO_MIN);
    setup();
    taskYIELD();
    while (true) {
        loop ();
        taskYIELD();
    }
}
