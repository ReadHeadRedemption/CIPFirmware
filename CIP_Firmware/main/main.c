#include <stdio.h>

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

static const char *TAG = "MAIN";


//Included to host files on esp32 memory for testing purposes, will be removed when SD card is implemented
#include "esp_spiffs.h"
char *spiffs_file = "/spiffs/sample.gcode";


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

void StepperTestTask(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting stepper motor test...");
    
    // Test pattern: oscillate each motor with increasing frequency
    uint32_t test_frequencies[] = {100, 500, 1000, 2000};  // Hz
    int freq_index = 0;

    while(1)
    {
        // Cycle through different frequencies
        uint32_t freq = test_frequencies[freq_index];
        
        ESP_LOGI(TAG, "Testing motors at %lu Hz", freq);
        
        // Start all motors
        stepper_set_frequency(MOTOR_X, freq);
        stepper_set_frequency(MOTOR_Y, freq);
        stepper_set_frequency(MOTOR_Z, freq);
        stepper_set_frequency(MOTOR_E, freq);
        
        // Run for 5 seconds at this frequency
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        
        // Stop all motors
        stepper_set_frequency(MOTOR_X, 0);
        stepper_set_frequency(MOTOR_Y, 0);
        stepper_set_frequency(MOTOR_Z, 0);
        stepper_set_frequency(MOTOR_E, 0);
        
        ESP_LOGI(TAG, "Stopping motors for 2 seconds...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        
        // Move to next frequency
        freq_index = (freq_index + 1) % 4;
    }
}

void parserTask(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting G-code parser task...");
    parse(spiffs_file);
    ESP_LOGI(TAG, "G-code parsing completed");
    vTaskDelete(NULL); // Delete task after parsing is done
}

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


    // Initialize stepper motors
    if (stepper_motor_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize stepper motors!");
        return;
    }
    
    // Create heater control task
    xTaskCreate(HeaterControl, "HeaterControl", 2048, NULL, 1, NULL);
    
    // Create stepper motor test task
    xTaskCreate(StepperTestTask, "StepperTest", 2048, NULL, 2, NULL);

    // Create G-code parser task
    xTaskCreate(parserTask, "GCodeParser", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully");
}
