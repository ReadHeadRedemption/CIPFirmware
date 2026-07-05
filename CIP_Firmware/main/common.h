#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

//ESP32 Headers
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_io_expander_gpio_wrapper.h"
#include "pins.h"
#include "IOExpander.h"
#include "driver/uart.h"

//Include RTOS Headers for real time management of the firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


// Global Variables
// Struct to define a multi-axis move command

typedef struct {
    //Cartesian cordinates that the movement command will try to target
    float target_x;
    float target_y;
    float target_z;

    // how far the lead screw should push ink out
    float moveE;

    //Polar cordinate information for arc movement
    float center_x;
    float center_y;
    bool circleDir;

    uint32_t feed_rate_hz; // Max speed of the lead axis
} MoveCmd_t;

extern MoveCmd_t head;

extern QueueHandle_t uart_queue;
extern uart_event_t event;

// Semaphores
extern SemaphoreHandle_t xSwitchSemaphore;
extern SemaphoreHandle_t ySwitchSemaphore;
extern SemaphoreHandle_t zSwitchSemaphore;
extern SemaphoreHandle_t eSwitchSemaphore;
extern SemaphoreHandle_t StartButtonSemaphore;
extern SemaphoreHandle_t TestButtonSemaphore;
extern QueueHandle_t     FileName;
extern SemaphoreHandle_t parseSemaphore; 
extern SemaphoreHandle_t SDCardMutex;
extern SemaphoreHandle_t allowMove;
extern SemaphoreHandle_t i2c_mutex;
extern SemaphoreHandle_t nohead;
extern TaskHandle_t parserHandle;

extern esp_io_expander_handle_t io_expander;


#endif 