#ifndef COMMON_H
#define COMMON_H

#define TXD_PIN (GPIO_NUM_40)
#define RXD_PIN (GPIO_NUM_38)
#define UART_PORT_NUM (UART_NUM_1)
#define BAUD_RATE (9600)

#include <stdio.h>

//ESP32 Headers
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "pins.h"
#include "driver/uart.h"

//Include RTOS Headers for real time management of the firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern QueueHandle_t uart_queue;
extern uart_event_t event;

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

// Semaphores
extern SemaphoreHandle_t xSwitchSemaphore;
extern SemaphoreHandle_t ySwitchSemaphore;
extern SemaphoreHandle_t zSwitchSemaphore;
extern SemaphoreHandle_t eSwitchSemaphore;
extern SemaphoreHandle_t StartButtonSemaphore;
extern SemaphoreHandle_t TestButtonSemaphore;
extern SemaphoreHandle_t parseSemaphore; 

#endif 