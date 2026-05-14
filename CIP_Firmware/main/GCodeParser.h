#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "common.h"
#include "stepperMotor.h"
void parse(char *fileLocation);

typedef enum {
    X = 0,
    Y = 1,
    Z = 2,
    E = 3
} coordinate_t;


#endif