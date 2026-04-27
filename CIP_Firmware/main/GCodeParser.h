#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void parse(char *fileLocation);

typedef enum {
    X = 0,
    Y = 1,
    Z = 2,
    E = 3
} coordinate_t;

#endif