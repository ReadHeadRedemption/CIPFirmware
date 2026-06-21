#include "main.h"
#include "driver/uart.h"
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include <ctype.h>
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

char *tempFile = "main\\sample.gcode";
char *spiffs_file = "/spiffs/sample.gcode";

///////////////////////////////////////////////////////////////////////////////
//
//                          TASKS
//
///////////////////////////////////////////////////////////////////////////////
void HeaterControl(void *pvParameters)
{
    while (1)
    {
        // Read temperature from ADC
        // Compare with desired temperature
        // Control SSR accordingly
        vTaskDelay(100 / portTICK_PERIOD_MS); // Delay for 100ms
    }
}

void parserTask(void *pvParameters)
{
    while (1)
    {
        if (xSemaphoreTake(StartButtonSemaphore, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "Starting G-code parser task...");
            readParseFile(spiffs_file);
            ESP_LOGI(TAG, "G-code parsing completed");
        }
    }
}

static void vTelemetryTask(void *pvParameters)
{
    uint32_t counter = 0;
    char buffer[32];

    ESP_LOGI(TAG, "Telemetry task started.");

    // while (1) {
    //     // Formulate sample operational metrics
    //     snprintf(buffer, sizeof(buffer), "Cycles: %lu", counter++);
        
    //     // Pass to our decoupled display driver
    //     display_update_status(buffer);

    //     // Sleep to mimic data collection intervals (e.g., 250ms)
    //     vTaskDelay(pdMS_TO_TICKS(250));
    // }
}

static void console_task(void *arg) {
    (void)arg;
    char *line;

    while (1) {
        // printf("> ");
        fflush(stdout);
        // Read line with prompt
        line = linenoise("esp> ");
        if (line == NULL) {
            // Free buffer
            linenoiseFree(line);
            continue; // Empty line
        }
        // Process line
        if (strcmp(line, "home") == 0) {
            homeMotors();
        } else if (strcmp(line, "test") == 0) {
            ESP_LOGI(TAG, "Test command received");
        } else {
           parse(line);
        }
        // Add to history
        linenoiseHistoryAdd(line);
        // Free buffer
        linenoiseFree(line);
    }
}

///////////////////////////////////////////////////////////////////////////////
//
//                          TEST TASKS
//
///////////////////////////////////////////////////////////////////////////////
void moveMotor(void *pvParameters)
{
    if (xSemaphoreTake(TestButtonSemaphore, portMAX_DELAY) == pdTRUE)
    {
        ESP_LOGI(TAG, "Starting motor test task...");
        homeMotors();
        MoveCmd_t moveTo;
        moveTo.target_x = 0.0f;
        moveTo.target_y = 0.0f;
        moveTo.target_z = 0.0f;
        moveTo.feed_rate_hz = 10000.0f;
        while (1)
        {
            if (xSemaphoreTake(TestButtonSemaphore, portMAX_DELAY) == pdTRUE)
            {
                ESP_LOGI(TAG, "Test button pressed, incrementing motor");
                vTaskDelay(pdMS_TO_TICKS(1000));
                moveTo.target_x += 20.0f;
                moveTo.target_y += 20.0f;
                moveTo.target_z += 20.0f;

                ESP_LOGI(TAG, "moving motor to X: %.3f Y: %.3f Z: %.3f", moveTo.target_x, moveTo.target_y, moveTo.target_z);
                vTaskDelay(pdMS_TO_TICKS(1000));
                coordinated_move(&moveTo);
                ESP_LOGI(TAG, "Finished Moving");
                // Reset test button semaphore for next test
            }
        }
    }
}

void testYaxis(void *pvParameters)
{
    homeMotors();
    MoveCmd_t moveTo;
    moveTo.feed_rate_hz = 1000.0f;
    moveTo.target_z = 5.0f;
    moveTo.target_x = 100.0f;
    moveTo.target_y = 0.0f;
    coordinated_move(&moveTo);
    moveTo.feed_rate_hz = 1000.0f;
    while(1)
    {
        moveTo.target_y -= .1;
        coordinated_move(&moveTo);
    }
}

void buttonTest(void *pvParameters)
{
    while (1)
    {
        if (xSemaphoreTake(TestButtonSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            ESP_LOGI(TAG, "Test Button Pressed");
        }
        if (xSemaphoreTake(StartButtonSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            ESP_LOGI(TAG, "Start Button Pressed");
        }
        if ((xSemaphoreTake(xSwitchSemaphore, pdMS_TO_TICKS(10)) == pdTRUE))
        {
            ESP_LOGI(TAG, "X Switch Pressed");
        }
        if ((xSemaphoreTake(ySwitchSemaphore, pdMS_TO_TICKS(10)) == pdTRUE))
        {
            ESP_LOGI(TAG, "Y Switch Pressed");
        }
        if ((xSemaphoreTake(zSwitchSemaphore, pdMS_TO_TICKS(10)) == pdTRUE))
        {
            ESP_LOGI(TAG, "Z Switch Pressed");
        }
    }
}
static char *skip_ws(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}
// static void console_task(void *arg) {
//     (void)arg;
//     char* line;

//     // print_help();
//     while (1) {
//         // printf("> ");
//         fflush(stdout);
//         // Read line with prompt
//         line = linenoise("esp> ");
//         if (line == NULL) {
//             // Free buffer
//             linenoiseFree(line);
//             continue; // Empty line
//         }
//         // Process line
//         printf("Received: %s\n", line);

//         line[strcspn(line, "\r\n")] = '\0';
//         char *cmd = skip_ws(line);
//         printf("Received: %s\n", line);

//         if (*cmd == '\0') {
//             // Free buffer
//             linenoiseFree(line);
//             continue;
//         }

//         // if (strcasecmp(cmd, "help") == 0) {
//         //     print_help();
//         // }
//         // else if (strcasecmp(cmd, "status") == 0) {
//         //     print_status();
//         // }
//         // else if (strcasecmp(cmd, "stop") == 0) {
//         //     xSemaphoreGive(stop_semaphore);
//         //     ESP_LOGW(TAG, "Stop requested");
//         // }
//         // else if (strncasecmp(cmd, "run ", 4) == 0) {
//         //     char *path = skip_ws(cmd + 4);
//         //     run_gcode_file(path);
//         // }
//         // else
//         // {
//         //     execute_gcode_line(cmd);
//         // }

//         // Add to history
//         linenoiseHistoryAdd(line);
//         // Free buffer
//         linenoiseFree(line);
//     }
// }
///////////////////////////////////////////////////////////////////////////////
//
//                          INTERRUPT HANDLES
//
///////////////////////////////////////////////////////////////////////////////
void IRAM_ATTR xISRHandler(void *arg)
{
    //ESP_LOGV(TAG, "X Limit Switch Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xSwitchSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

void IRAM_ATTR yISRHandler(void *arg)
{
    //ESP_LOGV(TAG, "Y Limit Switch Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(ySwitchSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

void IRAM_ATTR zISRHandler(void *arg)
{
    //ESP_LOGV(TAG, "Z Limit Switch Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(zSwitchSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

void IRAM_ATTR StartButtonISRHandler(void *arg)
{
    //ESP_LOGV(TAG, "Start Button Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    //xSemaphoreGiveFromISR(StartButtonSemaphore, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}
void IRAM_ATTR TestButtonISRHandler(void *arg)
{
    //ESP_LOGV(TAG, "Test Button Triggered \n");
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    //xSemaphoreGiveFromISR(TestButtonSemaphore, &xHigherPrioTaskWoken);
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

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_new_repl_uart(&uart_config, &repl_config, &repl);

    // Initialize SPIFFS for file storage (for testing G-code parsing)
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
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

    // Setting Pin Directions
    ESP_LOGI(TAG, "Configuring GPIO pins...");
    ESP_LOGI(TAG, "Configuring Output pins...");
    // Configure stepper direction pins as outputs
    gpio_config_t outputPins = {
        .pin_bit_mask = (1ULL << xDir) | (1ULL << yDir) |
                        (1ULL << zDir) | (1ULL << eDir) |
                        (1ULL << xStep) | (1ULL << yStep) |
                        (1ULL << zStep) | (1ULL << eStep) |
                        (1ULL << tempEnable) | (1ULL << eEnable),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&outputPins);
    ESP_LOGI(TAG, "Configuring Input pins...");
    gpio_config_t inputPins = {
        .pin_bit_mask = (1ULL << xSwitch) | (1ULL << ySwitch) |
                        (1ULL << zSwitch) |
                        (1ULL << StartButton) | (1ULL << TestButton),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&inputPins);
    ESP_LOGI(TAG, "GPIO pins configured successfully");

    gpio_install_isr_service(0);
    gpio_set_intr_type(xSwitch, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(xSwitch, xISRHandler, NULL);
    gpio_set_intr_type(ySwitch, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(ySwitch, yISRHandler, NULL);
    gpio_set_intr_type(zSwitch, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(zSwitch, zISRHandler, NULL);
    gpio_set_intr_type(StartButton, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(StartButton, StartButtonISRHandler, NULL);
    gpio_set_intr_type(TestButton, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(TestButton, TestButtonISRHandler, NULL);

    ESP_LOGI(TAG, "GPIO interrupts configured successfully");

    ESP_ERROR_CHECK(gpio_config(&outputPins));
    uart_config_t uart_config2 = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(UART_PORT_NUM, &uart_config2);
    
    // Set UART pins (using -1 for pins you don't want to change)
    uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    // Configure UART parameters
    const int uart_buffer_size = (1024 * 2);
    uart_driver_install(UART_PORT_NUM, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0);

    //////////////////////////////////////////////////////////////////////////
    //
    //                          TASK CREATION
    //
    /////////////////////////////////////////////////////////////////////////
    // Initialize stepper motors
    if (stepper_motor_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize stepper motors!");
        return;
    }
    ESP_LOGI(TAG, "Booting display framework...");
    
    gpio_set_level(tempEnable, 1);
    gpio_set_level(eEnable, 1);
    xTaskCreatePinnedToCore(display_task, "display_tsk", 4096, NULL, 3, NULL, 1);
    
    // Test Tasks
    xTaskCreate(moveMotor, "MoveMotor", 4096, NULL, 2, NULL);
    //xTaskCreate(testYaxis, "stop yaxis grinding", 4096, NULL, 2, NULL);
    //xTaskCreate(buttonTest, "Testing button inputs", 2048, NULL, 2, NULL);
    xTaskCreate(console_task, "ConsoleTask", 4096, NULL, 1, NULL);
    

    //Create heater control task
    // xTaskCreate(HeaterControl, "HeaterControl", 2048, NULL, 1, NULL);
    // Create G-code parser task
    xTaskCreate(parserTask, "GCodeParser", 8192, NULL, 3, NULL);
    // xTaskCreatePinnedToCore(console_task, "console_task", 8192, NULL, 4, NULL, 0);
    //xTaskCreatePinnedToCore(vTelemetryTask, "telemetry_tsk", 3072, NULL, 5, NULL, 1);

    // ESP_LOGI(TAG, "All tasks created successfully");
}
