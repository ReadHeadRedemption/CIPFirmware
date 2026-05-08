#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <math.h>

//ESP32 Headers
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "pins.h"

//Include RTOS Headers for real time management of the firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

//Motor Control Headers
#include "stepperMotor.h"

//G-code Headers
#include "GCodeParser.h"


//Included to host files on esp32 memory for testing purposes, 
//will be removed when SD card is implemented
#include "esp_spiffs.h"


// Global Variables
// Semaphores
extern SemaphoreHandle_t xSwitchSemaphore;
extern SemaphoreHandle_t ySwitchSemaphore;
extern SemaphoreHandle_t zSwitchSemaphore;
extern SemaphoreHandle_t eSwitchSemaphore;
extern SemaphoreHandle_t StartButtonSemaphore;
extern SemaphoreHandle_t TestButtonSemaphore;
extern SemaphoreHandle_t parseSemaphore; 


#endif