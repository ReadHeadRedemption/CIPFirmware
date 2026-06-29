// heater_control.h
#pragma once
#ifndef HEATER_CONTROL_H
#define HEATER_CONTROL_H

#include "common.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <max31865.h>

// #define SSR_PIN             GPIO_NUM_4   // placeholder — swap to real pin

#define HEATER_HYSTERESIS   2.0f         // ±2°C dead band to prevent rapid switching

// call once from app_main before creating the task
esp_err_t heater_init(void);

// FreeRTOS task — pass target temp as float* via pvParameters
void HeaterControl();
#endif // HEATER_CONTROL_H