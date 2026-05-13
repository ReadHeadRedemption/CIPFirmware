#include "main.h"
static const char *TAG = "MAIN";

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

SemaphoreHandle_t xSwitchSemaphore = NULL;
SemaphoreHandle_t ySwitchSemaphore = NULL;
SemaphoreHandle_t zSwitchSemaphore = NULL;
SemaphoreHandle_t eSwitchSemaphore = NULL;
SemaphoreHandle_t StartButtonSemaphore = NULL;
SemaphoreHandle_t TestButtonSemaphore = NULL;

char *tempFile = "main\\sample.gcode";
char *spiffs_file = "/spiffs/sample.gcode";

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


void parserTask(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting G-code parser task...");
    parse(spiffs_file);
    ESP_LOGI(TAG, "G-code parsing completed");
    vTaskDelete(NULL); // Delete task after parsing is done
}

//MiscFunctions
void moveMotor(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting motor test task...");
    MoveCmd_t moveTo; 
    moveTo.target_x = 0.0f;
    moveTo.target_y = 0.0f;
    moveTo.target_z= 0.0f;
    moveTo.feed_rate_hz = 5000.0;
    while(1)
    {
        if(xSemaphoreTake(TestButtonSemaphore, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "Test button pressed, incrementing motor");
            vTaskDelay(pdMS_TO_TICKS(1000));
            moveTo.target_x = 10.0f;
            moveTo.target_y = 10.0f;
            moveTo.target_z = 10.0f;
            ESP_LOGI(TAG, "moving motor...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            coordinated_move(&moveTo);
            ESP_LOGI(TAG, "Finished Moving");
            // Reset test button semaphore for next test
        }
    }
}

void buttonTest(void *pvParameters)
{
    while(1)
    {
        if(xSemaphoreTake(TestButtonSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            ESP_LOGI(TAG, "Test Button Pressed");
        }
        if(xSemaphoreTake(StartButtonSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            ESP_LOGI(TAG, "Start Button Pressed");
        }
        if(xSemaphoreTake(xSwitchSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            ESP_LOGI(TAG, "X Switch Pressed");
        }
        if(xSemaphoreTake(ySwitchSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            ESP_LOGI(TAG, "Y Switch Pressed");
        }
        if(xSemaphoreTake(zSwitchSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            ESP_LOGI(TAG, "Z Switch Pressed");
        }
    }
}
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
void IRAM_ATTR TestButtonISRHandler(void *arg) {
    ESP_LOGV(TAG,"Test Button Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(TestButtonSemaphore, &xHigherPrioTaskWoken);
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
    TestButtonSemaphore = xSemaphoreCreateBinary();

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
                        (1ULL << zSwitch) | (1ULL << eSwitch) | 
                        (1ULL << StartButton) | (1ULL << TestButton),
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
    gpio_set_intr_type(TestButton, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(TestButton, TestButtonISRHandler, NULL);

    ESP_LOGI(TAG, "GPIO interrupts configured successfully");

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
    xTaskCreate(moveMotor, "MoveMotor", 4096, NULL, 2, NULL);
    //xTaskCreate(buttonTest, "Testing button inputs", 2048, NULL, 2, NULL);
    // Create heater control task
    //xTaskCreate(HeaterControl, "HeaterControl", 2048, NULL, 1, NULL);

    // Create G-code parser task
    //xTaskCreate(parserTask, "GCodeParser", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "All tasks created successfully");
}
