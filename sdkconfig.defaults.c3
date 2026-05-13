CONFIG_ESP32C3_DEFAULT_CPU_FREQ_160=y
CONFIG_ESP32C3_DEFAULT_CPU_FREQ_MHZ=160

# Task watchdog: armed at runtime with main task as the only subscriber.
# Idle-task subscription is disabled so a healthy idle task cannot mask a wedged main.
CONFIG_ESP_TASK_WDT=y
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n
