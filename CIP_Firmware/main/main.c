#include "main.h"
#include "IOExpander.h"

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

// char *tempFile = "main\\sample.gcode";
// char *spiffs_file = "/spiffs/sample.gcode";

///////////////////////////////////////////////////////////////////////////////
//
//                          TASKS
//
///////////////////////////////////////////////////////////////////////////////
// void HeaterControl(void *pvParameters)
// {
//     while (1)
//     {
//         // Read temperature from ADC
//         // Compare with desired temperature
//         // Control SSR accordingly
//         vTaskDelay(100 / portTICK_PERIOD_MS); // Delay for 100ms
//     }
// }

void parserTask(void *pvParameters)
{
    BaseType_t recieveFlag;
    char fileString[128];
    while (1)
    {
        // printf("hi");
        recieveFlag = xQueueReceive(FileName, &fileString, 1); // recieve values from the queue
        if (recieveFlag == pdPASS)
        {
            ESP_LOGI(TAG, "I HAVE READ FROM THE QUEUE");
            if (xSemaphoreTake(StartButtonSemaphore, portMAX_DELAY) == pdTRUE)
            {

                ESP_LOGI(TAG, "Starting G-code parser task...");
                char filepath[256];
                snprintf(filepath, sizeof(filepath), "/sdcard/%s", fileString);
                ESP_LOGI(TAG, "FILE LOCATION: %s", filepath);
                readParseFile(filepath);
            }
        }
    }
    // while (1)
    // {
    //     if (xSemaphoreTake(StartButtonSemaphore, portMAX_DELAY) == pdTRUE)
    //     {
    //         ESP_LOGI(TAG, "Starting G-code parser task...");
    //         readParseFile(spiffs_file);
    //         ESP_LOGI(TAG, "G-code parsing completed");
    //     }
    // }
    vTaskDelete(NULL);
}

static void checkSwitches(void *pvParameters)
{
    int levels[4] = {0};
    ESP_LOGI(TAG, "STARTING LIMIT SWITCH TASK");
    while (1)
    {
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            levels[0] = gpio_get_level(xSwitch);
            levels[1] = gpio_get_level(ySwitch);
            levels[2] = gpio_get_level(zSwitch);
            levels[3] = gpio_get_level(eSwitch);
            // ESP_LOGI(TAG, "SWITCH LEVELS READ: %d, %d, %d, %d", levels[0], levels[1], levels[2], levels[3]);
            xSemaphoreGive(i2c_mutex);
        }
        if (levels[0] == 1)
        {
            xSemaphoreGive(xSwitchSemaphore);
            printf("X switch triggered\n");
        }
        if (levels[1] == 1)
        {
            xSemaphoreGive(ySwitchSemaphore);
            printf("Y switch triggered\n");
        }
        if (levels[2] == 1)
        {

            xSemaphoreGive(zSwitchSemaphore);
            printf("Z switch triggered\n");
        }
        if (levels[3] == 1)
        {
            xSemaphoreGive(eSwitchSemaphore);
            // printf("E switch triggered\n");
        }
        vTaskDelay(pdMS_TO_TICKS(19));
    }
    vTaskDelete(NULL);
}

static void console_task(void *arg)
{
    (void)arg;
    char *line;

    while (1)
    {
        // printf("> ");
        fflush(stdout);
        // Read line with prompt
        line = linenoise("esp> ");
        if (line == NULL)
        {
            // Free buffer
            linenoiseFree(line);
            continue; // Empty line
        }
        else if (strcmp(line, "test") == 0)
        {
            ESP_LOGI(TAG, "Test command received");
            printf("\n");
        }
        // Process line
        else if (strcmp(line, "home") == 0)
        {
            homeMotors();
            printf("\n");
        }
        else if (strcmp(line, "sdinfo") == 0)
        {
            sdInfo();
            printf("\n");
        }
        else if (strcmp(line, "stop") == 0)
        {
            vTaskSuspend(parserHandle);
            parse("G1 E-600");
            ESP_LOGI(TAG, "STOPPING");
            printf("\n");
        }
        else if (strcmp(line, "head?") == 0)
        {
            int state = readHeadState();
            printf("TOOL HEAD: %d", state);
            printf("\n");
        }
        else if (strcmp(line, "where") == 0)
        {
            position();
            printf("\n");
        }
        else
        {
            parse(line);
        }
        // Add to history
        linenoiseHistoryAdd(line);
        // Free buffer
        linenoiseFree(line);
    }
}

static void blinker(void *pvParameters)
{
    int level = 0;
    while (1)
    {
        gpio_set_level(LED1, !level);
        vTaskDelay(pdMS_TO_TICKS(500));
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

///////////////////////////////////////////////////////////////////////////////
//
//                          APP MAIN
//
//////////////////////////////////////////////////////////////////////////////

void app_main(void)
{
    ESP_LOGI(TAG, "CIP Firmware starting...");
    gpio_set_direction(xStep, GPIO_MODE_OUTPUT);
    gpio_set_direction(yStep, GPIO_MODE_OUTPUT);
    gpio_set_direction(zStep, GPIO_MODE_OUTPUT);
    gpio_set_direction(eStep, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED1, GPIO_MODE_OUTPUT);

    // gpio_config_t outputPins = {
    //     .pin_bit_mask = (1ULL << xStep) | (1ULL << yStep) |
    //                     (1ULL << zStep) | (1ULL << eStep),
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE,
    // };
    // gpio_config(&outputPins);

    ///////////////////////////////////////////////////////////////////////////////
    //
    //                          ININTIALIZING COMPONENTS
    //
    ///////////////////////////////////////////////////////////////////////////////

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_new_repl_uart(&uart_config, &repl_config, &repl);

    // Initialize SPIFFS for file storage (for testing G-code parsing)
    // esp_vfs_spiffs_conf_t conf = {
    //     .base_path = "/spiffs",
    //     .partition_label = "spiffs",
    //     .max_files = 5,
    //     .format_if_mount_failed = true,
    // };

    // esp_err_t ret = esp_vfs_spiffs_register(&conf);
    // if (ret != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to mount SPIFFS");
    //     return;
    // }
    // ESP_LOGI(TAG, "SPIFFS mounted successfully");

    vTaskDelay(pdMS_TO_TICKS(100));
    if (init_io_expander() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize IO Expander!");
        return;
    }

    if (stepper_motor_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize stepper motors!");
        return;
    }

    if (sdmmc_host_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SDMMC host!");
        return;
    }

    if (initializeSD() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SD card!");
        // return;
    }

    // Creating button semaphores
    StartButtonSemaphore = xSemaphoreCreateBinary();
    TestButtonSemaphore = xSemaphoreCreateBinary();

    // Creating limit switch semaphores
    xSwitchSemaphore = xSemaphoreCreateBinary();
    ySwitchSemaphore = xSemaphoreCreateBinary();
    zSwitchSemaphore = xSemaphoreCreateBinary();
    eSwitchSemaphore = xSemaphoreCreateBinary();
    allowMove = xSemaphoreCreateBinary();
    nohead = xSemaphoreCreateBinary();

    // Creating semaphore for waiting for temperature to be reached
    tempReachedSemaphore = xSemaphoreCreateBinary();

    // Mutexes
    SDCardMutex = xSemaphoreCreateMutex();
    i2c_mutex = xSemaphoreCreateMutex();

    // Queues
    FileName = xQueueCreate(1, (sizeof(char) * 128));
    temperature_queue = xQueueCreate(1, sizeof(float));

    ///////////////////////////////////////////////////////////////////////////////
    //
    //                          PIN CONFIGURATIONS
    //
    ///////////////////////////////////////////////////////////////////////////////

    // Setting Pin Directions
    ESP_LOGI(TAG, "Configuring GPIO pins...");
    ESP_LOGI(TAG, "Configuring Output pins...");
    // Configure stepper direction pins as outputs

    ESP_LOGI(TAG, "Configuring Input pins...");
    // gpio_config_t inputPins = {
    //     .pin_bit_mask = (1ULL << xSwitch) | (1ULL << ySwitch) |
    //                     (1ULL << zSwitch),
    //     .mode = GPIO_MODE_INPUT,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .pull_up_en = GPIO_PULLUP_ENABLE,
    //     .intr_type = GPIO_INTR_DISABLE,
    // };
    // gpio_config(&inputPins);
    uart_config_t uart_config2 = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(UART_NUM_1, &uart_config2);

    // Set UART pins (using -1 for pins you don't want to change)
    uart_set_pin(UART_NUM_1, PI_TX, PI_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Configure UART parameters
    const int uart_buffer_size = (1024 * 2);
    uart_driver_install(UART_NUM_1, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0);

    ESP_LOGI(TAG, "GPIO pins configured successfully");

    // ESP_ERROR_CHECK(gpio_config(&outputPins));

    //////////////////////////////////////////////////////////////////////////
    //
    //                          TASK CREATION
    //
    /////////////////////////////////////////////////////////////////////////
    // Initialize stepper motors

    // xTaskCreatePinnedToCore(display_task, "display_tsk", 8192, NULL, 3, NULL, 1);

    display_init();

    heater_init();

    // Test Tasks
    xTaskCreate(moveMotor, "MoveMotor", 4096, NULL, 5, NULL);
    // xTaskCreate(testYaxis, "stop yaxis grinding", 4096, NULL, 2, NULL);
    // xTaskCreate(buttonTest, "Testing button inputs", 2048, NULL, 2, NULL);
    xTaskCreate(console_task, "ConsoleTask", 4096, NULL, 1, NULL);
    // xTaskCreate(printExpanderState, "print expander state", 4096, NULL, 2, NULL);
    //  Create heater control task
    xTaskCreate(HeaterControl, "HeaterControl", 4096, NULL, 1, NULL);
    //   Create G-code parser task
    xTaskCreate(parserTask, "GCodeParser", 8192, NULL, 5, &parserHandle);
    xTaskCreate(blinker, "Blinks LED Heartbeat", 2048, NULL, 1, NULL);
    xTaskCreate(checkSwitches, "poll limit switches", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "All tasks created successfully");
}
