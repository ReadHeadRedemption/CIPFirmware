#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "common.h"
#include "stepperMotor.h"

extern MoveCmd_t head;

typedef enum {
    X = 0,
    Y = 1,
    Z = 2,
    E = 3
} coordinate_t;


void readParseFile(char *fileLocation);
void parse(char *line);

extern int current_head_id;
extern int totalLines;
extern int readLines;

typedef void (*LineCountCallback)(int readLines, int totalLines);
typedef void (*HeadChangeCallback)(int headID);

void setLineCountCallback(LineCountCallback callback);
void setHeadCallback(HeadChangeCallback callback);

#endif