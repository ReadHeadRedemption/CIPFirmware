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
    while(1)
    {
        // Start all motors
        stepper_set_frequency(MOTOR_X, 100);
        stepper_set_frequency(MOTOR_Y, 500);
        stepper_set_frequency(MOTOR_Z, 1000);
        stepper_set_frequency(MOTOR_E, 2000);
        // Run for 5 seconds at this frequency
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        stepper_set_frequency(MOTOR_X, 0);
        stepper_set_frequency(MOTOR_Y, 0);
        stepper_set_frequency(MOTOR_Z, 0);
        stepper_set_frequency(MOTOR_E, 0);
        vTaskDelay(5000 / portTICK_PERIOD_MS);

    }
}

// void parserTask(void *pvParameters)
// {
//     ESP_LOGI(TAG, "Starting G-code parser task...");
//     parse(spiffs_file);
//     ESP_LOGI(TAG, "G-code parsing completed");
//     vTaskDelete(NULL); // Delete task after parsing is done
// }


// void PinTestTask(void *pvParameters)
// {
//     uint8_t state = 0;
//     while(1)
//     {
//         // Toggle all step pins
//         gpio_set_level(X_STEP, state);
//         gpio_set_level(Y_STEP, state);
//         gpio_set_level(Z_STEP, state);
//         gpio_set_level(EXTRUDER_STEP, state);
//         gpio_set_level(X_DIR, state);
//         gpio_set_level(Y_DIR, state);
//         gpio_set_level(Z_DIR, state);
//         gpio_set_level(EXTRUDER_DIR, state);
//         ESP_LOGI(TAG, "Toggled step and direction pins to state: %d", state);
//         state = !state; // Toggle state
//         vTaskDelay(500 / portTICK_PERIOD_MS); // Delay for 1 second
//     }
// }

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

    //Setting Pin Directions
    ESP_LOGI(TAG, "Configuring GPIO pins...");
    ESP_LOGI(TAG, "Configuring Output pins...");
    // Configure stepper direction pins as outputs
    gpio_config_t outputPins = {
        .pin_bit_mask = (1ULL << X_DIR) | (1ULL << Y_DIR) | (1ULL << Z_DIR) | (1ULL << EXTRUDER_DIR) |
                        (1ULL << X_STEP)| (1ULL << Y_STEP)| (1ULL << Z_STEP)| (1ULL << EXTRUDER_STEP),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&outputPins);
    
    //ESP_ERROR_CHECK(gpio_config(&outputPins));



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
    //xTaskCreate(parserTask, "GCodeParser", 4096, NULL, 3, NULL);

    //xTaskCreate(PinTestTask, "PinTest", 2048, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully");
}
