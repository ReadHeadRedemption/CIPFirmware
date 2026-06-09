#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <math.h>

//ESP32 Headers
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "pins.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"

//Include RTOS Headers for real time management of the firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "common.h"

//Motor Control Headers
#include "stepperMotor.h"

//G-code Headers
#include "GCodeParser.h"

//Display Headers
#include "display.h"

//Included to host files on esp32 memory for testing purposes, 
//will be removed when SD card is implemented
#include "esp_spiffs.h"

#endif