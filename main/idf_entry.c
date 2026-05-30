#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    // This tiny app only makes the repository open as a normal ESP-IDF project.
    // Use `idf.py rg-image` or `idf.py rg-flash` to build the actual firmware.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
