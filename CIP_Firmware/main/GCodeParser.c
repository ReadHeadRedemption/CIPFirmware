#include "GCodeParser.h"
#include "esp_log.h"
#include "IOExpander.h"
#include "esp_task_wdt.h"

// #define DEBUG_PURGE

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
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
        {
            gpio_set_level(zEnable, 0);
            gpio_set_level(eEnable, 0);
            xSemaphoreGive(i2c_mutex);
        }
#ifdef DEBUG_PURGE
        char up[] = "G0 Z5.5";

        char mini_extrude[] = "G1 E40 F200";
        // char down[] = "G0 Z0.5";

        // TEMPORARY CODE FOR INITIAL EXTRUSION
        parse(up);
        parse(mini_extrude);
        for (int i = 0; i < 10; i++)
        {
            parse(mini_extrude);
            vTaskDelay(pdMS_TO_TICKS(2500));
        }
        // vTaskDelay(pdMS_TO_TICKS(10000));
        // parse(down);

#endif /* DEBUG_PURGE */

        char *found = strstr(fgets(line, sizeof(line), file), "TOTAL_LINES:");

        if (found != NULL)
        {
            sscanf(found, "TOTAL_LINES:%d", &totalLines);
            printf("Total lines: %d\n", totalLines);
        }
        rewind(file);

        while (fgets(line, sizeof(line), file))
        {
            parse(line);
            // vTaskDelay(pdMS_TO_TICKS(5000));
            readLines++;
            if (lineCountCb != NULL)
            {
                lineCountCb(readLines, totalLines); // Notify display immediately
            }
        }
        fclose(file);
        ESP_LOGI(TAG, "FINISH READING FILE");
    }
}

void parse(char *line)
{
    bool pullback = true;
    char *p;
    char cmd[8] = "";
    float cords[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    bool coordChange[3] = {false, false, false};
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
                    head.feed_rate_hz = 4000;
                }
                if ((p = strchr(line, 'E')) != NULL)
                {
                    sscanf(p + 1, "%f", &extrude);
                    head.moveE += extrude * scale * eScale;
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
                }
                head.feed_rate_hz = 200;
                coordinated_move(&head);
                head.feed_rate_hz = 250;
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
                    head.moveE -= extrude * (scale * eScale * 0.15f); // Pull back 15% of the extruded amount
                    // head.moveE = 0;
                    ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f",
                             head.target_x, head.target_y, head.target_z);
                    ESP_LOGI(TAG, "PUSHING HEAD TO %.3f (%.5f) MM", head.moveE, head.moveE);
                    coordinated_move(&head);
                    head.feed_rate_hz = 500;
                }
                // WAITING FOR 100 MILLISECONDS TO LET SOLDER PASTE STICK
                if (fabs(lastLocation[X] - head.target_x) > 1.0f || fabs(lastLocation[Y] - head.target_y) > 1.0f)
                    vTaskDelay(pdMS_TO_TICKS(100));
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
            char data_to_receive[100];

            printf(line + 5);
            int bytes_written = 0;
            if (strcmp(cmd, "M240") == 0)
            {
                printf("HERE!!");
                char capture[] = "CAPTURE\n";
                bytes_written = uart_write_bytes(UART_NUM_1, capture, strlen(capture));
            }
            else
            {
                printf(line + 5);
                bytes_written = uart_write_bytes(UART_NUM_1, line + 5, strlen(line + 5));
            }
            // int bytes_written = uart_write_bytes(UART_PORT_NUM, data, strlen(data));

            printf("%d", bytes_written);
            printf("HELOO");

            if (xQueueReceive(uart_queue, (void *)&event, (TickType_t)portMAX_DELAY))
            {
                uart_read_bytes(UART_NUM_1, data_to_receive, event.size, portMAX_DELAY);
                ESP_LOGI(TAG, "%s", data_to_receive);
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
        head.target_x = 0;
        head.target_y = 0;
        head.target_z = 100 * scale;
        coordinated_move(&head);

        // Disable extruder to swap toolhead
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
        {
            gpio_set_level(eEnable, 1);
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
            char *toolIndex[] = {"Conductive Ink", "Insulator Ink", "Camera", NULL};
            ESP_LOGI(TAG, "CALLING T: TOOL CHANGE TO %s", toolIndex[tool]);
            // changeTool(tool);

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
            ESP_LOGI(TAG, "Tool change successful.");
        }
    }
}
/*
gscrib G-Code list
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/
