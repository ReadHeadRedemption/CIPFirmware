#ifndef GCODES_H
#define GCODES_H

#include "main.h"
#include "GcodeParser.h"
#include "stepperMotor.h"

void G0(float x, float y, float z, float e); // fast move to a position
void G1(float x, float y, float z, float e); // linear move  
void G21(); // set length mm
void G28(); //find home position
void G90(); // set distance mode absolute
void G92(); // set axis potion
void FeedRate(int FR); // Set extrude push rate (step frequency)


#endif