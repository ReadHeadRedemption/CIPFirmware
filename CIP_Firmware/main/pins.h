#ifndef PINS_H
#define PINS_H

#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "IOExpander.h"

////////////////////////////////////////////////////////////////////
//
//                  SPI PINS
//
////////////////////////////////////////////////////////////////////

#define MISO GPIO_NUM_13
#define MOSI GPIO_NUM_11
#define SCK  GPIO_NUM_12

////////////////////////////////////////////////////////////////////
//
//                  IO EXPANDER PINS
//
////////////////////////////////////////////////////////////////////

#define SDA GPIO_NUM_1
#define SCL GPIO_NUM_2


////////////////////////////////////////////////////////////////////
//
//                  DISPLAY PINS
//
////////////////////////////////////////////////////////////////////
#define reset GPIO_NUM_21 
#define dcDisplay GPIO_NUM_9 
#define csTouch GPIO_NUM_8 
#define csDisplay GPIO_NUM_15

////////////////////////////////////////////////////////////////////
//
//                  SD CARD PINS
//
////////////////////////////////////////////////////////////////////

#define SD_CMD GPIO_NUM_10
#define SD_CLK GPIO_NUM_38
#define SD_D0 GPIO_NUM_39
#define SD_D1 GPIO_NUM_40
#define SD_D2 GPIO_NUM_41
#define SD_D3 GPIO_NUM_42

////////////////////////////////////////////////////////////////////
//
//                  STEPPER MOTOR PINS
//
////////////////////////////////////////////////////////////////////

// X Pins
#define xDir (GPIO_NUM_MAX + 12) // xxx IO Expander P14
#define xStep GPIO_NUM_4
#define xEnable (GPIO_NUM_MAX + 8) // xxx IO Expander P10

// Y Pins
#define yDir (GPIO_NUM_MAX + 13) // xxx IO Expander P15
#define yStep GPIO_NUM_5
#define yEnable (GPIO_NUM_MAX + 9) // xxx IO Expander P11

// Z Pins
#define zDir (GPIO_NUM_MAX + 14) // xxx IO Expander P16
#define zStep GPIO_NUM_6
#define zEnable (GPIO_NUM_MAX + 10) // xxx IO Expander P12

// Extruder Pins
#define eDir (GPIO_NUM_MAX + 15) // xxx IO Expander P17
#define eStep GPIO_NUM_7
#define eEnable (GPIO_NUM_MAX + 11) // xxx IO Expander P13

// Limit Switch Pins
#define xSwitch (GPIO_NUM_MAX + 3) // IO Expander P3
#define ySwitch (GPIO_NUM_MAX + 4) // IO Expander P4
#define zSwitch (GPIO_NUM_MAX + 5) // IO Expander P5
#define eSwitch (GPIO_NUM_MAX + 6) // IO Expander P6

////////////////////////////////////////////////////////////////////
//
//                  HEATER PINS
//
////////////////////////////////////////////////////////////////////

#define RTDCS  GPIO_NUM_16 
#define SSRE (GPIO_NUM_MAX + 2) // IO expander P02

////////////////////////////////////////////////////////////////////
//
//                  HEAD ID PINS
//
////////////////////////////////////////////////////////////////////

#define HEAD_ID_0 (GPIO_NUM_MAX + 0) // IO Expander P00
#define HEAD_ID_1 (GPIO_NUM_MAX + 1) // IO Expander P01

////////////////////////////////////////////////////////////////////
//
//                  RASPBERRY PI COMMUNICATION PINS
//
////////////////////////////////////////////////////////////////////

#define PI_TX GPIO_NUM_18   // UART1 RX
#define PI_RX GPIO_NUM_17   // UART1 TX

////////////////////////////////////////////////////////////////////
//
//                  MISCILANEOUS PINS
//
////////////////////////////////////////////////////////////////////

#define LED1 GPIO_NUM_48
#define FAN1 (GPIO_NUM_MAX + 7) // IO Expander P07
#endif  // PINS_H