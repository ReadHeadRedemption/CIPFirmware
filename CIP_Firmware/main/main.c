#include <stdio.h>
static const char *TAG = "MAIN";

//ESP32 Headers
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "pins.h"

//Include RTOS Headers for real time management of the firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

//Motor Control Headers
#include "stepperMotor.h"

//G-code Headers
#include "GCodeParser.h"
char *tempFile = "main\\sample.gcode";

//Included to host files on esp32 memory for testing purposes, 
//will be removed when SD card is implemented
#include "esp_spiffs.h"
char *spiffs_file = "/spiffs/sample.gcode";

// Global Variables
// Semaphores
SemaphoreHandle_t xSwitchSemaphore;
SemaphoreHandle_t ySwitchSemaphore;
SemaphoreHandle_t zSwitchSemaphore;
SemaphoreHandle_t eSwitchSemaphore;
SemaphoreHandle_t StartButtonSemaphore;

// Position in mm for each axis
float xPosition = 100;
float yPosition = 100;
float zPosition = 100;


/*
List of Tasks
4 Extruder Motors:
- X axis
- Y axis
- Z axis
- Extruder
  - Needs to read the ink head tool change from G-code
Heat Bed
- turn on/off ssr 
- detect temperature
Screen Display
Touch Interface
Reading SD card task
Limit Switch Interrupts -- Homeing function

*/

// Task Lists
void HeaterControl(void *pvParameters)
{
    while(1)
    {
        //Read temperature from ADC
        //Compare with desired temperature
        //Control SSR accordingly
        vTaskDelay(100 / portTICK_PERIOD_MS); // Delay for 100ms
    }
}

void stepperHome(void *pvParameters)
{
    xSemaphoreTake(StartButtonSemaphore, portMAX_DELAY); // Wait for start button to be pressed
    // Home X axis
    ESP_LOGI(TAG, "Homing X axis...");
    stepper_set_direction(MOTOR_X, 0); // Move towards limit switch
    stepper_set_frequency(MOTOR_X, globalFrequency);
    vTaskDelay(50 / portTICK_PERIOD_MS); // Prevent from sitting on button
    stepper_set_direction(MOTOR_X, 1); // Move towards limit switch
    stepper_set_frequency(MOTOR_X, globalFrequency);
    xSemaphoreTake(xSwitchSemaphore, portMAX_DELAY); // Wait for X limit switch to trigger
    stepper_set_frequency(MOTOR_X, 0); // Stop motor
    xPosition = 0; // Reset position to 0 after homing
    ESP_LOGI(TAG, "X axis homed successfully");
    // Home Y axis
    ESP_LOGI(TAG, "Homing Y axis...");
    stepper_set_direction(MOTOR_Y, 1); // Move towards limit switch
    stepper_set_frequency(MOTOR_Y, globalFrequency);
    vTaskDelay(50 / portTICK_PERIOD_MS); // Prevent from sitting on button
    stepper_set_direction(MOTOR_Y, 0);
    stepper_set_frequency(MOTOR_Y, globalFrequency);
    xSemaphoreTake(ySwitchSemaphore, portMAX_DELAY);
    stepper_set_frequency(MOTOR_Y, 0);
    yPosition = 0;
    ESP_LOGI(TAG, "Y axis homed successfully");
    // Home Z axis
    ESP_LOGI(TAG, "Homing Z axis...");
    stepper_set_direction(MOTOR_Z, 1); // Move towards limit switch
    stepper_set_frequency(MOTOR_Z, globalFrequency);
    vTaskDelay(50 / portTICK_PERIOD_MS); // Prevent from sitting on button
    stepper_set_direction(MOTOR_Z, 0);
    stepper_set_frequency(MOTOR_Z, globalFrequency);
    xSemaphoreTake(zSwitchSemaphore, portMAX_DELAY);
    stepper_set_frequency(MOTOR_Z, 0);
    zPosition = 0;
    ESP_LOGI(TAG, "Z axis homed successfully");
    // Home Extruder axis (if needed)
    ESP_LOGI(TAG, "Homing Extruder axis...");
    stepper_set_direction(MOTOR_E, 1);
    stepper_set_frequency(MOTOR_E, globalFrequency);
    xSemaphoreTake(eSwitchSemaphore, portMAX_DELAY);
    stepper_set_frequency(MOTOR_E, 0);
    ESP_LOGI(TAG, "Extruder axis homed successfully");
    vTaskDelete(NULL); // Delete task after homing is done
}

// void parserTask(void *pvParameters)
// {
//     ESP_LOGI(TAG, "Starting G-code parser task...");
//     parse(spiffs_file);
//     ESP_LOGI(TAG, "G-code parsing completed");
//     vTaskDelete(NULL); // Delete task after parsing is done
// }


///////////////////////////////////////////////////////////////////////////////
//
//                          INTERRUPT HANDLES
//
//////////////////////////////////////////////////////////////////////////////
void IRAM_ATTR xISRHandler(void *arg) {
    ESP_LOGV(TAG,"X Limit Switch Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xSwitchSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

void IRAM_ATTR yISRHandler(void *arg) {
    ESP_LOGV(TAG,"Y Limit Switch Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(ySwitchSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

void IRAM_ATTR zISRHandler(void *arg) {
    ESP_LOGV(TAG,"Z Limit Switch Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(zSwitchSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

void IRAM_ATTR eISRHandler(void *arg) {
    ESP_LOGV(TAG,"E Limit Switch Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(eSwitchSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}
void IRAM_ATTR StartButtonISRHandler(void *arg) {
    ESP_LOGV(TAG,"Start Button Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(StartButtonSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

///////////////////////////////////////////////////////////////////////////////
//
//                          APP MAIN
//
//////////////////////////////////////////////////////////////////////////////

void app_main(void)
{
    ESP_LOGI(TAG, "CIP Firmware starting...");

    // Initialize SPIFFS for file storage (for testing G-code parsing)
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS");
        return;
    }
    ESP_LOGI(TAG, "SPIFFS mounted successfully");

    // Creating limit switch semaphores 
    xSwitchSemaphore = xSemaphoreCreateBinary();
    ySwitchSemaphore = xSemaphoreCreateBinary();
    zSwitchSemaphore = xSemaphoreCreateBinary();
    eSwitchSemaphore = xSemaphoreCreateBinary();
    StartButtonSemaphore = xSemaphoreCreateBinary();

    ///////////////////////////////////////////////////////////////////////////////
    //
    //                          PIN CONFIGURATIONS
    //
    //////////////////////////////////////////////////////////////////////////////

    //Setting Pin Directions
    ESP_LOGI(TAG, "Configuring GPIO pins...");
    ESP_LOGI(TAG, "Configuring Output pins...");
    // Configure stepper direction pins as outputs
    gpio_config_t outputPins = {
        .pin_bit_mask = (1ULL << xDir) | (1ULL << yDir) | 
                        (1ULL << zDir) | (1ULL << eDir) |
                        (1ULL << xStep)| (1ULL << yStep)| 
                        (1ULL << zStep)| (1ULL << eStep),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&outputPins);
    ESP_LOGI(TAG, "Configuring Input pins...");
    gpio_config_t inputPins = {
        .pin_bit_mask = (1ULL << xSwitch) | (1ULL << ySwitch) | 
                        (1ULL << zSwitch) | (1ULL << eSwitch) | (1ULL << StartButton),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&inputPins);
    ESP_LOGI(TAG, "GPIO pins configured successfully");

    gpio_install_isr_service(0);
    gpio_set_intr_type(xSwitch, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(xSwitch, xISRHandler, NULL);
    gpio_set_intr_type(ySwitch, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(ySwitch, yISRHandler, NULL);
    gpio_set_intr_type(zSwitch, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(zSwitch, zISRHandler, NULL);
    gpio_set_intr_type(eSwitch, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(eSwitch, eISRHandler, NULL);
    gpio_set_intr_type(StartButton, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(StartButton, StartButtonISRHandler, NULL);

    //ESP_ERROR_CHECK(gpio_config(&outputPins));

    //////////////////////////////////////////////////////////////////////////
    //
    //                          TASK CREATION
    //
    /////////////////////////////////////////////////////////////////////////
    // Initialize stepper motors
    if (stepper_motor_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize stepper motors!");
        return;
    }
    xTaskCreate(stepperHome, "StepperHome", 2048, NULL, 1, NULL);
    
    // Create heater control task
    xTaskCreate(HeaterControl, "HeaterControl", 2048, NULL, 1, NULL);

    // Create G-code parser task
    //xTaskCreate(parserTask, "GCodeParser", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "All tasks created successfully");
}
