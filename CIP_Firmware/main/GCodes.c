#include "main.h"
#include "GCodes.h"

/*
gscrib G-Code list 
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/

const static char *TAG = "G_CODES";

/*
G92 X0 Y0 Z0 ; Set axis position
G21 ; Set length units, millimeters
G90 ; Set distance mode, absolute
F500
G0 Z10
G0 X0 Y0
*/
///////////////////////////////////////////////////////////////////////////////
//
//                          GCODE COMMANDS
//
//////////////////////////////////////////////////////////////////////////////

void G0(float x, float y, float z, float e) // quick move to a position
{

}

void G1(float x, float y, float z, float e) // move to a position
{
    MoveCmd_t target; 
    target.target_x = x;
    target.target_y = y;
    target.target_z = z;
    coordinated_move(&target);
}

void G21() // set length mm
{

}

void G28() //find home position
{
    // Home X axis
    ESP_LOGI(TAG, "Homing X axis...");
    stepper_set_direction(MOTOR_X, 0); // Move towards limit switch
    stepper_set_frequency(MOTOR_X, 1000); 
    vTaskDelay(50 / portTICK_PERIOD_MS); // Prevent from sitting on button
    stepper_set_direction(MOTOR_X, 1); // Move towards limit switch
    stepper_set_frequency(MOTOR_X, 1000);
    xSemaphoreTake(xSwitchSemaphore, portMAX_DELAY); // Wait for X limit switch to trigger
    stepper_set_frequency(MOTOR_X, 0); // Stop motor
    ESP_LOGI(TAG, "X axis homed successfully");
    // Home Y axis
    ESP_LOGI(TAG, "Homing Y axis...");
    stepper_set_direction(MOTOR_Y, 1); // Move towards limit switch
    stepper_set_frequency(MOTOR_Y, 1000);
    vTaskDelay(50 / portTICK_PERIOD_MS); // Prevent from sitting on button
    stepper_set_direction(MOTOR_Y, 0);
    stepper_set_frequency(MOTOR_Y, 1000);
    xSemaphoreTake(ySwitchSemaphore, portMAX_DELAY);
    stepper_set_frequency(MOTOR_Y, 0);
    ESP_LOGI(TAG, "Y axis homed successfully");
    // Home Z axis
    ESP_LOGI(TAG, "Homing Z axis...");
    stepper_set_direction(MOTOR_Z, 1); // Move towards limit switch
    stepper_set_frequency(MOTOR_Z, 1000);
    vTaskDelay(50 / portTICK_PERIOD_MS); // Prevent from sitting on button
    stepper_set_direction(MOTOR_Z, 0);
    stepper_set_frequency(MOTOR_Z, 1000);
    xSemaphoreTake(zSwitchSemaphore, portMAX_DELAY);
    stepper_set_frequency(MOTOR_Z, 0);
    ESP_LOGI(TAG, "Z axis homed successfully");
    // Home Extruder axis (if needed)
    ESP_LOGI(TAG, "Homing Extruder axis...");
    stepper_set_direction(MOTOR_E, 1);
    stepper_set_frequency(MOTOR_E, 1000);
    xSemaphoreTake(eSwitchSemaphore, portMAX_DELAY);
    stepper_set_frequency(MOTOR_E, 0);
    ESP_LOGI(TAG, "Extruder axis homed successfully");
}

void G90() // set distance mode absolute
{

}

void G92() // set axis potion
{

}

void FeedRate(int FR) // Set extrude push rate (step frequency)
{
    stepper_set_frequency(MOTOR_E, FR);
}