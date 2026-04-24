#ifndef PINS_H
#define PINS_H

#include "hal/gpio_types.h"

//SPI Pins
#define MISO GPIO_NUM_15
#define MOSI GPIO_NUM_2
#define SCK  GPIO_NUM_4
#define CS_TOUCH GPIO_NUM_16
#define CS_DISPLAY GPIO_NUM_17

//Stepper Motor Pins
#define X_STEP GPIO_NUM_13
#define X_DIR GPIO_NUM_12

#define Y_STEP GPIO_NUM_14
#define Y_DIR GPIO_NUM_27

#define Z_STEP GPIO_NUM_26
#define Z_DIR GPIO_NUM_25

#define EXTRUDER_STEP GPIO_NUM_33
#define EXTRUDER_DIR GPIO_NUM_32
//Heater Pins

// Limit Switch Pins
#define X_LIMIT_SWITCH GPIO_NUM_5
#define Y_LIMIT_SWITCH GPIO_NUM_18
#define Z_LIMIT_SWITCH GPIO_NUM_19
#define PROBE_SWITCH GPIO_NUM_21

// SD Card Pins

// HeadID Pins
#define HEAD_ID_0 GPIO_NUM_22
#define HEAD_ID_1 GPIO_NUM_23

// Raspberry Pi Communication Pins
#define RPI_UART_TX GPIO_NUM_1   // UART0 TX
#define RPI_UART_RX GPIO_NUM_3   // UART0 RX

#endif  // PINS_H