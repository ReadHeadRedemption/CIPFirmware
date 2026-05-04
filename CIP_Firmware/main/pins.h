#ifndef PINS_H
#define PINS_H

#include "driver/gpio.h"
#include "driver/adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


//SPI Pins
// #define MISO GPIO_NUM_15
// #define MOSI GPIO_NUM_2
// #define SCK  GPIO_NUM_4
// #define CS_TOUCH GPIO_NUM_16
// #define CS_DISPLAY GPIO_NUM_17

//Stepper Motor Pins
#define xStep GPIO_NUM_17
#define xDir GPIO_NUM_16
//#define xEnable GPIO_NUM_4

#define yStep GPIO_NUM_18
#define yDir GPIO_NUM_5
//#define yEnable GPIO_NUM_5

#define zStep GPIO_NUM_21
#define zDir GPIO_NUM_19
//#define zEnable GPIO_NUM_6

#define eStep GPIO_NUM_25
#define eDir GPIO_NUM_26
//#define eEnable GPIO_NUM_7
// //Heater Pins


// // Limit Switch Pins
#define xSwitch GPIO_NUM_15
#define ySwitch GPIO_NUM_22
#define zSwitch GPIO_NUM_4
#define eSwitch GPIO_NUM_13

// // SD Card Pins

// // HeadID Pins
// #define HEAD_ID_0 GPIO_NUM_22
// #define HEAD_ID_1 GPIO_NUM_23

// // Raspberry Pi Communication Pins
// #define RPI_UART_TX GPIO_NUM_1   // UART0 TX
// #define RPI_UART_RX GPIO_NUM_3   // UART0 RX

// Misc
#define StartButton GPIO_NUM_23
#define TestButton GPIO_NUM_34


/**
 * @brief Configure GPIO pins for the system
 */
void pinConfiguations(void);

/**
 * @brief Test task for pin testing
 */
void PinTestTask(void *pvParameters);

#endif  // PINS_H