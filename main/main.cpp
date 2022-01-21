#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task.h"

extern void setup();
extern void loop();
extern "C" unsigned long millis();

void yield()
{
    static unsigned long last = 0;
    static constexpr unsigned long wait =  0.8f * CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000; 
    unsigned long now = millis();

    if(now - last >= wait) {
        last = now;
        vTaskDelay(2);
    }
}

extern "C" void app_main()
{
    vTaskPrioritySet(NULL, ESP_TASK_PRIO_MIN + 1);
    setup();
    while (true) {
        loop ();
        yield();
    }
}
