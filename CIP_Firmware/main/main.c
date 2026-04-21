#include <stdio.h>

//ESP32 Headers
#include "driver/gpio.h"
#include "driver/adc.h"
#include "pins.h"

//Include RTOS Headers for real time management of the firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"



//Motor Control Headers

//


/*
List of Tasks
4 Extruder Motors:
- X axis
- Y axis
- Z axis
- Extruder
  - Needs to read the ink head tool change from G-code
Heat Bed
- turn on/off ssr 
- detect temperature
Screen Display
Touch Interface
Reading SD card task
Limit Switch Interrupts -- Homeing function

*/



void HeaterControl(void *pvParameters)
{
    while(1)
    {
        //Read temperature from ADC
        //Compare with desired temperature
        //Control SSR accordingly
        vTaskDelay(100 / portTICK_PERIOD_MS); // Delay for 100ms
    }
}


void app_main(void)
{

xTaskCreate(HeaterControl, "Task 1", 2048, NULL, 1, NULL);

}

