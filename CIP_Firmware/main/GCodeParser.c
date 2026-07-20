#include "GCodeParser.h"
#include "esp_log.h"
#include "IOExpander.h"
#include "esp_task_wdt.h"

#define DEBUG_PURGE

static const char *TAG = "GCODE_PARSER";

uint32_t feed = 0;

float scale = 1.0f;
float eScale = 0.0001f;
float extrude = 0.0f;
float total_extruded = 0.0f;

bool distMode = true;

static LineCountCallback lineCountCb = NULL;
static HeadChangeCallback headCallback = NULL;

void setLineCountCallback(LineCountCallback callback)
{
    lineCountCb = callback;
}
int totalLines = -1;
int readLines = 0;

void setHeadCallback(HeadChangeCallback callback)
{
    headCallback = callback;
}
int current_head_id = 3;

int eStepSize = 40;
int purgeCount = 20;

#define UART_PORT UART_NUM_1
#define UART_PENDING_SIZE 1024

static uint8_t uart_pending[UART_PENDING_SIZE];
static size_t uart_pending_len = 0;


/*
 * Try to pull ONE COMPLETE message out of uart_pending.
 *
 * A complete message ends with:
 *
 *      '\n'
 *
 * or:
 *
 *      '\0'
 *
 * Returns true ONLY if a complete message was found.
 */
static bool uart_extract_complete_message(
    char *output,
    size_t output_size)
{
    if (output == NULL || output_size < 2)
    {
        return false;
    }

    for (size_t i = 0; i < uart_pending_len; i++)
    {
        if ((uart_pending[i] == '\n') ||
            (uart_pending[i] == '\0'))
        {
            size_t output_index = 0;

            /*
             * Copy message into output.
             *
             * Ignore '\r' so both:
             *
             *     \n
             *
             * and:
             *
             *     \r\n
             *
             * work.
             */
            for (size_t j = 0; j < i; j++)
            {
                if (uart_pending[j] == '\r')
                {
                    continue;
                }

                if (output_index < output_size - 1)
                {
                    output[output_index++] =
                        (char)uart_pending[j];
                }
            }

            output[output_index] = '\0';

            /*
             * Remove this message from pending buffer.
             *
             * Anything AFTER the newline stays in the buffer
             * for the next call.
             */
            size_t consumed = i + 1;

            size_t remaining =
                uart_pending_len - consumed;

            if (remaining > 0)
            {
                memmove(
                    uart_pending,
                    &uart_pending[consumed],
                    remaining
                );
            }

            uart_pending_len = remaining;

            return true;
        }
    }

    return false;
}

static bool uart_wait_for_complete_response(
    char *response,
    size_t response_size)
{
    uart_event_t event;

    if (response == NULL || response_size < 2)
    {
        return false;
    }

    response[0] = '\0';

    /*
     * FIRST:
     *
     * See whether a complete message is already in our
     * software receive buffer.
     */
    if (uart_extract_complete_message(
            response,
            response_size))
    {
        ESP_LOGI(
            TAG,
            "UART complete response: [%s]",
            response
        );

        return true;
    }


    /*
     * KEEP WAITING until an ACTUAL COMPLETE MESSAGE exists.
     *
     * xQueueReceive() returning DOES NOT cause this function
     * to return.
     */
    while (1)
    {
        /*
         * This deliberately blocks.
         *
         * The next command CANNOT be transmitted by this task
         * until this function returns.
         */
        if (xQueueReceive(
                uart_queue,
                &event,
                portMAX_DELAY) != pdTRUE)
        {
            continue;
        }


        /*
         * We only care about received UART data here.
         */
        if (event.type == UART_DATA)
        {
            size_t bytes_remaining = event.size;

            while (bytes_remaining > 0)
            {
                uint8_t temp[128];

                size_t requested =
                    bytes_remaining < sizeof(temp)
                    ? bytes_remaining
                    : sizeof(temp);

                /*
                 * Read only the amount associated with
                 * this UART_DATA event.
                 */
                int length = uart_read_bytes(
                    UART_PORT,
                    temp,
                    requested,
                    portMAX_DELAY
                );

                if (length <= 0)
                {
                    break;
                }

                bytes_remaining -= length;


                /*
                 * Add received bytes to persistent software
                 * buffer.
                 */
                for (int i = 0; i < length; i++)
                {
                    if (uart_pending_len <
                        sizeof(uart_pending))
                    {
                        uart_pending[
                            uart_pending_len++
                        ] = temp[i];
                    }
                    else
                    {
                        ESP_LOGE(
                            TAG,
                            "UART pending buffer overflow"
                        );

                        uart_pending_len = 0;

                        return false;
                    }
                }


                /*
                 * THIS IS THE CRITICAL PART.
                 *
                 * Do NOT return just because UART_DATA arrived.
                 *
                 * Return ONLY if '\n' or '\0' was actually
                 * received and a COMPLETE response exists.
                 */
                if (uart_extract_complete_message(
                        response,
                        response_size))
                {
                    ESP_LOGI(
                        TAG,
                        "UART complete response: [%s]",
                        response
                    );

                    return true;
                }
            }
        }

        else if (event.type == UART_FIFO_OVF)
        {
            ESP_LOGE(TAG, "UART FIFO overflow");

            uart_flush_input(UART_PORT);
            xQueueReset(uart_queue);

            uart_pending_len = 0;

            return false;
        }

        else if (event.type == UART_BUFFER_FULL)
        {
            ESP_LOGE(TAG, "UART buffer full");

            uart_flush_input(UART_PORT);
            xQueueReset(uart_queue);

            uart_pending_len = 0;

            return false;
        }

        else if (event.type == UART_PARITY_ERR)
        {
            ESP_LOGE(TAG, "UART parity error");
        }

        else if (event.type == UART_FRAME_ERR)
        {
            ESP_LOGE(TAG, "UART frame error");
        }

        /*
         * Notice:
         *
         * WE DO NOT RETURN HERE.
         *
         * Go right back to xQueueReceive() and keep waiting.
         */
    }
}

static bool uart_send_and_wait(
    const char *command,
    char *response,
    size_t response_size)
{
    if (command == NULL)
    {
        return false;
    }

    ESP_LOGI(
        TAG,
        "TX >>> [%s]",
        command
    );


    /*
     * SEND ONE COMMAND
     */
    int written = uart_write_bytes(
        UART_NUM_1,
        command,
        strlen(command)
    );

    if (written < 0)
    {
        ESP_LOGE(
            TAG,
            "UART transmit failed"
        );

        return false;
    }


    /*
     * STOP HERE.
     *
     * Nothing after this point happens until the Pi sends
     * one COMPLETE response.
     */
    ESP_LOGI(
        TAG,
        "Waiting for Pi response..."
    );

    if (!uart_wait_for_complete_response(
            response,
            response_size))
    {
        ESP_LOGE(
            TAG,
            "Failed waiting for Pi response"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "RX <<< [%s]",
        response
    );

    return true;
}

void parsetoolparam(int tool)
{
    // cond3 sp cam
    float toolscale[] = {0.0005f, 0.00005, 0.0f};
    int eStep[] = {60, 1, 0};
    int purgeCounts[] = {40, 5, 0};

    eScale = toolscale[tool];
    eStepSize = eStep[tool];
    purgeCount = purgeCounts[tool];

    printf("Tool %d selected parameters updated\n", tool);
}

void readParseFile(char *fileLocation)
{
    parseSemaphore = xSemaphoreCreateMutex();
    FILE *file = fopen(fileLocation, "r");
    char line[128];
    totalLines = 0;
    readLines = 0;
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open G-code file: %s", fileLocation);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "Successfully opened G-code file: %s", fileLocation);
        // if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
        // {
        //     gpio_set_level(zEnable, 0);
        //     gpio_set_level(eEnable, 0);
        //     xSemaphoreGive(i2c_mutex);
        // }

        char *found = strstr(fgets(line, sizeof(line), file), "TOTAL_LINES:");

        if (found != NULL)
        {
            sscanf(found, "TOTAL_LINES:%d", &totalLines);
            printf("Total lines: %d\n", totalLines);
        }
        rewind(file);

        while (fgets(line, sizeof(line), file))
        {
            if (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdTRUE)
            {
                readLines++;
                parse(line);
                // vTaskDelay(pdMS_TO_TICKS(5000));
                if (lineCountCb != NULL)
                {
                    lineCountCb(readLines, totalLines); // Notify display immediately
                }
                xSemaphoreGive(parseSemaphore);
            }
        }
    }
    parse("M84"); // Disable motors after print
    fclose(file);
    ESP_LOGI(TAG, "FINISH READING FILE");
}

void parse(char *line)
{
    bool pullback = true;
    char *p;
    char cmd[8] = "";
    float cords[4] = {head.target_x, head.target_y, head.target_z, head.moveE};
    bool coordChange[3] = {false, false, false};

    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(zEnable, 0);
        xSemaphoreGive(i2c_mutex);
    }
    // First parse the command
    /*
    Command Types
    G
    M
    ; means comment
    */
    sscanf(line, "%7s", cmd);
    ESP_LOGI(TAG, "COMMAND: %s", cmd);

    char *commentStart = strchr(line, ';');
    if (commentStart != NULL)
    {
        *commentStart = '\0';
    }
    if (cmd[0] == 'F')
    {
        if ((p = strchr(line, 'F')) != NULL)
        {
            sscanf(p + 1, "%ld", &feed);
        }
        head.feed_rate_hz = feed;
    }
    else if (cmd[0] == 'G') // Not sure if this should be changed to a simple if. May be mutually exclusive regarldess
    {
        // start scaning for G type values
        if ((strcmp(cmd, "G0") == 0) || // rapid movement
            (strcmp(cmd, "G1") == 0))   // linear movement
        {
            if ((p = strchr(line, 'X')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[X]); // Use %f for float!
                coordChange[0] = true;
                ESP_LOGI(TAG, "Parsed X: %.3f", cords[X]);
                if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                {
                    gpio_set_level(xEnable, 0);
                    xSemaphoreGive(i2c_mutex);
                }
            }
            // Check Y
            if ((p = strchr(line, 'Y')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[Y]);
                ESP_LOGI(TAG, "Parsed Y: %.3f", cords[Y]);
                coordChange[1] = true;
                if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                {
                    gpio_set_level(yEnable, 0);
                    xSemaphoreGive(i2c_mutex);
                }
            }
            // Check Z
            if ((p = strchr(line, 'Z')) != NULL)
            {
                coordChange[2] = true;
                sscanf(p + 1, "%f", &cords[Z]);
                ESP_LOGI(TAG, "Parsed Z: %.3f", cords[Z]);
            }
            float lastLocation[3] = {head.target_x, head.target_y, head.target_z};
            /* COMMENTED OUT TEMPORARILY TO ADD DIFFERENT DEFAULT RATES FOR G0 (RAPID MOVE) VS G1 (PRINTING MOVE)*/
            // Check F
            // if ((p = strchr(line, 'F')) != NULL)
            // {
            //     sscanf(p + 1, "%ld", &feed);
            //     head.feed_rate_hz = feed;
            //     ESP_LOGI(TAG, "Parsed F: %ld", feed);
            // }
            int g = atoi(cmd + 1);
            switch (g)
            {
            case 0: // rapid movement
                ESP_LOGI(TAG, "CALLING G0");
                // Check F
                if ((p = strchr(line, 'F')) != NULL)
                {
                    sscanf(p + 1, "%ld", &feed);
                    head.feed_rate_hz = feed;
                    ESP_LOGI(TAG, "Parsed F: %ld", feed);
                }
                else
                {
                    head.feed_rate_hz = 5000;
                }
                if (distMode)
                {
                    if (coordChange[0])
                        head.target_x = cords[X] * scale;
                    if (coordChange[1])
                        head.target_y = cords[Y] * scale;
                    if (coordChange[2])
                        head.target_z = cords[Z] * scale;
                }
                else
                {
                    if (coordChange[0])
                        head.target_x += cords[X] * scale;
                    if (coordChange[1])
                        head.target_y += cords[Y] * scale;
                    if (coordChange[2])
                        head.target_z += cords[Z] * scale;
                }
                ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f",
                         head.target_x, head.target_y, head.target_z);
                coordinated_move(&head);
                break;
            case 1: // linear movement
                ESP_LOGI(TAG, "CALLING G1");
                float dE[3] = {0.0f};
                // NOTE: WITH THE WAY THE CODE IS WRITTEN, ANY FEED RATE INSERTION IS ESSENTIALLY IGNORED AND OVERWRITTEN
                if ((p = strchr(line, 'F')) != NULL)
                {
                    sscanf(p + 1, "%ld", &feed);
                    head.feed_rate_hz = feed;
                    ESP_LOGI(TAG, "Parsed G1 F: %ld", feed);
                }
                else
                {
                    head.feed_rate_hz = 500;
                }
                if ((p = strchr(line, 'E')) != NULL)
                {
                    sscanf(p + 1, "%f", &extrude);
                    head.moveE += extrude * scale * eScale;
                    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                    {
                        gpio_set_level(eEnable, 0);
                        xSemaphoreGive(i2c_mutex);
                    }
                }
                else
                {
                    for (int i = 0; i < 3; i++)
                    {
                        (dE[i] = fabs(lastLocation[i] - cords[i]));
                        // ESP_LOGI(TAG, "dE[%d]: %.3f", i, dE[i]);
                        // ESP_LOGI(TAG, "lastLocation[%d]: %.3f", i, lastLocation[i]);
                        // ESP_LOGI(TAG, "cords[%d]: %.3f", i, cords[i]);
                    }
                    extrude = (float)sqrt((dE[0] * dE[0]) +
                                          (dE[1] * dE[1]) +
                                          (dE[2] * dE[2]));
                    head.moveE += extrude * scale * eScale;
                    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                    {
                        gpio_set_level(eEnable, 0);
                        xSemaphoreGive(i2c_mutex);
                    }
                }
                head.feed_rate_hz = 100;
                coordinated_move(&head);
                head.feed_rate_hz = 50;
                if (distMode)
                {
                    if (coordChange[0])
                        head.target_x = cords[X] * scale;
                    if (coordChange[1])
                        head.target_y = cords[Y] * scale;
                    if (coordChange[2])
                        head.target_z = cords[Z] * scale;
                }
                else
                {
                    if (coordChange[0])
                        head.target_x += cords[X] * scale;
                    if (coordChange[1])
                        head.target_y += cords[Y] * scale;
                    if (coordChange[2])
                        head.target_z += cords[Z] * scale;
                }
                coordinated_move(&head);

                // Calculate the length of the move for E calculation

                ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f",
                         head.target_x, head.target_y, head.target_z);
                ESP_LOGI(TAG, "PUSHING HEAD TO %.3f (%.5f) MM", head.moveE, head.moveE);
                coordinated_move(&head);
                if (pullback)
                {
                    head.feed_rate_hz = 200;
                    head.moveE -= extrude * (scale * eScale * 0.0f); // Pull back 10% of the extruded amount
                    // head.moveE = 0;
                    ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f",
                             head.target_x, head.target_y, head.target_z);
                    ESP_LOGI(TAG, "PUSHING HEAD TO %.3f (%.5f) MM", head.moveE, head.moveE);
                    coordinated_move(&head);
                    head.feed_rate_hz = 500;
                }
                // WAITING FOR 100 MILLISECONDS TO LET SOLDER PASTE STICK
                if (fabs(lastLocation[X] - head.target_x) > 1.0f || fabs(lastLocation[Y] - head.target_y) > 1.0f)
                    vTaskDelay(pdMS_TO_TICKS(200));
                // printf("WAITING\n");
                total_extruded += extrude;
                break;
            default:
                break;
            }
            if ((fabs(lastLocation[X] - head.target_x) <= 1.0f || fabs(lastLocation[Y] - head.target_y) <= 1.0f))
            {
                if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                {
                    gpio_set_level(xEnable, 1);
                    gpio_set_level(yEnable, 1);
                    xSemaphoreGive(i2c_mutex);
                }
            }
        }
        else if ((strcmp(cmd, "G2") == 0) ||
                 (strcmp(cmd, "G3") == 0))
        {
            if ((p = strchr(line, 'X')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[X]); // Use %f for float!
                coordChange[0] = true;
                if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                {
                    gpio_set_level(xEnable, 0);
                    xSemaphoreGive(i2c_mutex);
                }
            }
            // Check Y
            if ((p = strchr(line, 'Y')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[Y]);
                coordChange[1] = true;
                if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                {
                    gpio_set_level(yEnable, 0);
                    xSemaphoreGive(i2c_mutex);
                }
            }
            float I = 0.0f, J = 0.0f;
            // Check I
            if ((p = strchr(line, 'I')) != NULL)
            {
                sscanf(p + 1, "%f", &I);
            }
            // Check J
            if ((p = strchr(line, 'J')) != NULL)
            {
                sscanf(p + 1, "%f", &J);
            }
            if ((p = strchr(line, 'E')) != NULL)
            {
                sscanf(p + 1, "%f", &extrude);

                head.moveE += extrude * scale * eScale;
            }
            int g = atoi(cmd + 1);
            // Set true for G2 (clockwise), false for G3 (counter-clockwise)
            head.circleDir = (g == 2) ? true : false;

            // Setting extrude based on linear distance of the arc move
            float lastLocation[4] = {head.target_x, head.target_y, head.center_x, head.center_y};
            float dE[4] = {0.0f};
            for (int i = 0; i < 4; i++)
                (dE[i] = fabs(lastLocation[i] - cords[i]));
            float extrude = (float)sqrt((dE[0] * dE[0]) +
                                        (dE[1] * dE[1]) +
                                        (dE[2] * dE[2]) +
                                        (dE[3] * dE[3])); // CHANGE TO ARC CALCULATION

            head.moveE += extrude * scale * eScale;

            if (coordChange[0])
                head.target_x = cords[X] * scale;
            if (coordChange[1])
                head.target_y = cords[Y] * scale;
            head.center_x = I;
            head.center_y = J;
            ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f WITH CENTER I:%.3f J:%.3f",
                     head.target_x, head.target_y, head.center_x, head.center_y);
            ESP_LOGI(TAG, "PUSHING HEAD %.3f MM", head.moveE);
            circular_move(&head);
            if ((fabs(lastLocation[X] - head.target_x) <= 1.0f || fabs(lastLocation[Y] - head.target_y) <= 1.0f))
            {
                if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
                {
                    gpio_set_level(xEnable, 1);
                    gpio_set_level(yEnable, 1);
                    xSemaphoreGive(i2c_mutex);
                }
            }
        }
        else if (strcmp(cmd, "G4") == 0) // delay in seconds
        {
            float delay = 0;
            if ((p = strchr(line, 'S')) != NULL)
            {
                sscanf(p + 1, "%f", &delay); // Use %f for float!
            }
            ESP_LOGI(TAG, "CALLING G4: DELAY FOR %.3f SECONDS", delay);
            vTaskDelay(pdMS_TO_TICKS(delay * 1000));
        }
        else if (strcmp(cmd, "G20") == 0) // Set Unit In
        {
            ESP_LOGI(TAG, "CALLING G20: UNITS INCHES");
            scale = 25.4;
        }
        else if (strcmp(cmd, "G21") == 0) // Set Unit MM
        {
            ESP_LOGI(TAG, "CALLING G21: UNITS MM");
            scale = 1;
        }
        else if (strcmp(cmd, "G28") == 0) // Home motors
        {
            ESP_LOGI(TAG, "CALLING G28: HOMING SEQUENCE");
            if ((p = strchr(line, 'X')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[X]); // Use %f for float!
                coordChange[X] = true;
                ESP_LOGI(TAG, "Parsed X: %.3f", cords[X]);
            }
            // Check Y
            if ((p = strchr(line, 'Y')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[Y]);
                ESP_LOGI(TAG, "Parsed Y: %.3f", cords[Y]);
                coordChange[Y] = true;
            }
            if (coordChange[X] || coordChange[Y])
            {
                head.target_x = cords[X] * scale;
                head.target_y = cords[Y] * scale;
                head.target_z = 100.0f * scale;
                coordinated_move(&head);
            }
            else
            {
                ESP_LOGI(TAG, "HOMING ALL MOTORS");
                homeMotors();
            }
        }
        else if (strcmp(cmd, "G90") == 0) // Set Distance Mode Absolute
        {
            ESP_LOGI(TAG, "CALLING G90: ABSOLUTE DISTANCE");
            distMode = true;
        }
        else if (strcmp(cmd, "G91") == 0) // Set Distance Mode Relative
        {
            ESP_LOGI(TAG, "CALLING G91: RELATIVE DISTANCE");
            distMode = false;
        }
    }
    else if (cmd[0] == 'M')
    {
        if (strcmp(cmd, "M0") == 0)
        {
        }
        else if (strcmp(cmd, "M84") == 0)
        {
            if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
            {
                gpio_set_level(xEnable, 1);
                gpio_set_level(yEnable, 1);
                gpio_set_level(zEnable, 1);
                gpio_set_level(eEnable, 1);
                xSemaphoreGive(i2c_mutex);
            }
            ESP_LOGI(TAG, "DISABLING MOTORS");
        }
        else if (strcmp(cmd, "M118") == 0 || strcmp(cmd, "M240") == 0)
        {
            char response[400];

            const char *command = line + 5;


            /* ============================================================
            * M240
            * ============================================================ */
            if (strcmp(cmd, "M240") == 0)
            {
                memset(response, 0, sizeof(response));

                /*
                * SEND:
                *
                *      CAPTURE\n
                *
                * Then STOP and wait for the Pi.
                */
                if (!uart_send_and_wait(
                        "CAPTURE\n",
                        response,
                        sizeof(response)))
                {
                    ESP_LOGE(
                        TAG,
                        "CAPTURE communication failed"
                    );
                }
            }


            /* ============================================================
            * END_LAYER
            * ============================================================ */
            else if (strstr(line, "END_LAYER") != NULL)
            {
                /*
                * First transmit END_LAYER.
                */
                ESP_LOGI(
                    TAG,
                    "TX >>> [%s]",
                    command
                );

                int written = uart_write_bytes(
                    UART_NUM_1,
                    command,
                    strlen(command)
                );

                if (written < 0)
                {
                    ESP_LOGE(
                        TAG,
                        "Failed sending END_LAYER"
                    );
                }
                else
                {
                    /*
                    * Pi will now send multiple results.
                    *
                    * ESP32 waits for ONE COMPLETE result.
                    *
                    * Then:
                    *
                    *      errorCallback()
                    *
                    * Then:
                    *
                    *      acknowledged\n
                    *
                    * Then wait for NEXT result.
                    */
                    while (1)
                    {
                        memset(
                            response,
                            0,
                            sizeof(response)
                        );

                        ESP_LOGI(
                            TAG,
                            "Waiting for END_LAYER result..."
                        );


                        /*
                        * BLOCK HERE UNTIL THE PI SENDS
                        * ONE COMPLETE MESSAGE.
                        */
                        if (!uart_wait_for_complete_response(
                                response,
                                sizeof(response)))
                        {
                            ESP_LOGE(
                                TAG,
                                "END_LAYER receive failed"
                            );

                            break;
                        }


                        ESP_LOGI(
                            TAG,
                            "END_LAYER RX <<< [%s]",
                            response
                        );


                        /*
                        * Check final marker FIRST.
                        */
                        if (strstr(
                                response,
                                "END_RESULT_TRANSMIT")
                            != NULL)
                        {
                            ESP_LOGI(
                                TAG,
                                "END_RESULT_TRANSMIT received"
                            );

                            uart_flush_input(UART_NUM_1);
                            xQueueReset(uart_queue);

                            break;
                        }


                        /*
                        * Process the received result.
                        */
                        // if (errorCallback != NULL)
                        // {
                        //     errorCallback(response);
                        // }


                        /*
                        * ONLY NOW acknowledge the result.
                        */
                        const char ack[] =
                            "acknowledged\n";

                        ESP_LOGI(
                            TAG,
                            "TX >>> [acknowledged]"
                        );

                        uart_write_bytes(
                            UART_NUM_1,
                            ack,
                            strlen(ack)
                        );


                        /*
                        * Loop returns immediately to:
                        *
                        * uart_wait_for_complete_response()
                        *
                        * Therefore we cannot send another ACK until
                        * another COMPLETE Pi message is received.
                        */
                    }
                }
            }


            /* ============================================================
            * NORMAL COMMAND
            * ============================================================ */
            else
            {
                memset(response, 0, sizeof(response));

                /*
                * THIS IS THE NORMAL HANDSHAKE:
                *
                *     ESP32 sends command
                *
                *             ↓
                *
                *     ESP32 STOPS HERE
                *
                *             ↓
                *
                *     Pi sends complete response
                *
                *             ↓
                *
                *     function returns
                *
                *             ↓
                *
                *     outer loop may send next command
                */
                if (!uart_send_and_wait(
                        command,
                        response,
                        sizeof(response)))
                {
                    ESP_LOGE(
                        TAG,
                        "UART command failed: [%s]",
                        command
                    );
                }
            }
        }
        else if (strcmp(cmd, "M140") == 0)
        {
            int temp = -1;
            if ((p = strchr(line, 'S')) != NULL)
            {
                ESP_LOGI(TAG, "BED TEMP NONBLOCKING");
                sscanf(p + 1, "%d", &temp);
                // Just set bed temp and keep going
            }
        }
        else if (strcmp(cmd, "M190") == 0)
        {
            float set_temp;
            // How to parse: "M190 S170" is an example where 170 is temperatyure in C
            if ((p = strchr(line, 'S')) != NULL)
            {
                sscanf(p + 1, "%f", &set_temp);
            }

            ESP_LOGI(TAG, "WAIT FOR BED TO REACH %f DEGREES", set_temp);
            xQueueSend(temperature_queue, &set_temp, 1);

            xSemaphoreTake(tempReachedSemaphore, portMAX_DELAY);

            printf("Tempature Reached\n");
        }
    }
    else if (cmd[0] == 'T') // tool change commands
    {
        parse("G0 X0 Y0 Z100");
        // Disable extruder to swap toolhead
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
        {
            gpio_set_level(eEnable, 1);
            // gpio_set_level(xEnable, 0);
            // gpio_set_level(yEnable, 0);
            xSemaphoreGive(i2c_mutex);
        }
        int tool = 0;
        if ((p = strchr(line, 'T')) != NULL)
        {
            sscanf(p + 1, "%d", &tool);
        }

        // 1. Safety Check: Prevent array out-of-bounds crashes
        if (tool < 0 || tool > 2)
        {
            ESP_LOGE(TAG, "Invalid tool index requested: %d", tool);
            // Handle error (e.g., return or break) depending on your loop structure
        }
        else
        {
            char *toolIndex[] = {"CONDUCTIVE INK", "SOLDER PASTE", "CAMERA", NULL};
            ESP_LOGI(TAG, "CALLING T: TOOL CHANGE TO %s", toolIndex[tool]);
            kfact(tool);
            parsetoolparam(tool);

            // 2. Fixed IO Check Loop
            if (io_expander == NULL)
            {
                ESP_LOGE(TAG, "THE HANDLE IS NULL RIGHT BEFORE USE!");
                while (1)
                {
                    vTaskDelay(10);
                } // Trap it here so it doesn't crash
            }
            int headID = readHeadState();
            while (headID != tool)
            {
                headID = readHeadState();
                if (headCallback != NULL)
                {
                    headCallback(headID);
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
            {
                gpio_set_level(eEnable, 0);
                xSemaphoreGive(i2c_mutex);
            }
            parse("G4 S3");
            parse("G28");

#ifdef DEBUG_PURGE
            char up[] = "G0 X0 Y200 Z60";

            char mini_extrude[50];
            snprintf(mini_extrude, sizeof(mini_extrude), "G1 E%d F200", eStepSize);

            // char down[] = "G0 Z0.5";

            // TEMPORARY CODE FOR INITIAL EXTRUSION
            parse(up);
            parse(mini_extrude);
            for (int i = 0; i < purgeCount; i++)
            {
                parse(mini_extrude);
                vTaskDelay(pdMS_TO_TICKS(2500));
            }
            vTaskDelay(pdMS_TO_TICKS(10000));
            // parse(down);

#endif /* DEBUG_PURGE */

            ESP_LOGI(TAG, "Tool change successful.");
        }
    }
}
/*
gscrib G-Code list
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/
