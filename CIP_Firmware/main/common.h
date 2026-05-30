#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

//ESP32 Headers
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "pins.h"

//Include RTOS Headers for real time management of the firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


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