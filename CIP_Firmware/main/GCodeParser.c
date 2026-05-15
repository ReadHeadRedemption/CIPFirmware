#include "GCodeParser.h"
#include "esp_log.h"

static const char *TAG = "GCODE_PARSER";

// Temp file location to load into the esp32
void parse(char *fileLocation)
{
    FILE *file = fopen(fileLocation, "r");
    char line[128];
    char *p;
    char cmd[4] = "";
    float cords[3] = {0.0f, 0.0f, 0.0f};

    uint32_t feed = 0;
    parseSemaphore = xSemaphoreCreateMutex();

    float scale = 1.0;
    bool distMode = true;
    MoveCmd_t head;
    head.target_x = cords[X] * scale;
    head.target_y = cords[Y] * scale;
    head.target_z = cords[Z] * scale;
    head.feed_rate_hz = 10000;
    parseSemaphore = xSemaphoreCreateMutex();

    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open G-code file: %s", fileLocation);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "Successfully opened G-code file: %s", fileLocation);
        while (fgets(line, sizeof(line), file))
        {
            float cords[3] = {0.0f, 0.0f, 0.0f};
            bool coordChange[3] = {false, false, false};
            // First parse the command
            /*
            Command Types
            G
            M
            ; means comment
            */
            sscanf(line, "%4s", cmd);
            ESP_LOGI(TAG, "COMMAND: %s", cmd);
            ESP_LOGI(TAG, "TYPE: %c", cmd[0]);
            if (strcmp(&cmd[0], ";") == 0 || strcmp(&cmd[0], " ") == 0) continue;
            else if (cmd[0] == 'F')
            {
                if ((p = strchr(line, 'F')) != NULL)
                    {
                        sscanf(p + 1, "%ld", &feed);
                    }
                head.feed_rate_hz = feed;
            }
            else if (cmd[0] == 'G')
            {
                // start scaning for G type values
                if ((strcmp(cmd, "G0") == 0) || // rapid movement
                    strcmp(cmd, "G1") == 0)     // linear movement
                {
                    if ((p = strchr(line, 'X')) != NULL)
                    {
                        sscanf(p + 1, "%f", &cords[X]); // Use %f for float!
                        coordChange[0] = true;
                    }
                    // Check Y
                    if ((p = strchr(line, 'Y')) != NULL)
                    {
                        sscanf(p + 1, "%f", &cords[Y]);
                        coordChange[1] = true;
                    }
                    // Check Z
                    if ((p = strchr(line, 'Z')) != NULL)
                    {
                        coordChange[2] = true;
                        sscanf(p + 1, "%f", &cords[Z]);
                    }
                    // Check F
                    if ((p = strchr(line, 'F')) != NULL)
                    {
                        sscanf(p + 1, "%ld", &feed);
                        head.feed_rate_hz = feed;

                    }
                    int g = atoi(cmd + 1);
                    switch (g)
                    {
                    case 0: // rapid movement
                        ESP_LOGI(TAG, "CALLING G0");
                        if (distMode)
                        {
                            if(coordChange[0])head.target_x = cords[X] * scale;
                            if(coordChange[1])head.target_y = cords[Y] * scale;
                            if(coordChange[2])head.target_z = cords[Z] * scale;
                        }
                        else
                        {
                            if(coordChange[0])head.target_x += cords[X] * scale;
                            if(coordChange[1])head.target_y += cords[Y] * scale;
                            if(coordChange[2])head.target_z += cords[Z] * scale;
                        }
                        ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f", 
                                    &head.target_x, &head.target_y, &head.target_z);
                        coordinated_move(&head);
                        break;
                    case 1: // linear movement
                        ESP_LOGI(TAG, "CALLING G1");
                        if (distMode)
                        {
                            if(coordChange[0])head.target_x = cords[X] * scale;
                            if(coordChange[1])head.target_y = cords[Y] * scale;
                            if(coordChange[2])head.target_z = cords[Z] * scale;
                        }
                        else
                        {
                            if(coordChange[0])head.target_x += cords[X] * scale;
                            if(coordChange[1])head.target_y += cords[Y] * scale;
                            if(coordChange[2])head.target_z += cords[Z] * scale;
                        }
                        stepper_set_direction(MOTOR_E,1);
                        stepper_set_frequency(MOTOR_E, feed);
                        ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f", 
                                    head.target_x, head.target_y, head.target_z);
                        coordinated_move(&head);
                        stepper_set_frequency(MOTOR_E,0);
                        break;
                    default:
                        break;
                    }
                }
                else if ((strcmp(cmd, "G2") == 0) || (strcmp(cmd, "G3") == 0))
                {

                }
                else if (strcmp(cmd, "G4") == 0) // delay in seconds
                {
                    int delay = 0;
                    if ((p = strchr(line, 'P')) != NULL)
                    {
                        sscanf(p + 1, "%d", &delay); // Use %f for float!
                    }
                    ESP_LOGI(TAG, "DELAY FOR %d SECONDS", delay);
                    vTaskDelay(pdMS_TO_TICKS(delay*1000));
                }
                else if (strcmp(cmd, "G20") == 0) // Set Unit In
                {
                    ESP_LOGI(TAG, "CALLING G20");
                    scale = 25.4;
                }
                else if (strcmp(cmd, "G21") == 0) // Set Unit MM
                {
                    ESP_LOGI(TAG, "CALLING G21");
                    scale = 1;
                }
                else if (strcmp(cmd, "G28") == 0) // Home motors
                {
                    ESP_LOGI(TAG, "CALLING G28");
                    homeMotors();
                }
                else if (strcmp(cmd, "G90") == 0) // Set Distance Mode Absolute
                {
                    distMode = true;
                }
                else if (strcmp(cmd, "G91") == 0) // Set Distance Mode Relative
                {
                    distMode = false;
                }
            }
            else if (strcmp(&cmd[0], "M") == 0)
            {
            }
        }
    }
}

/*
gscrib G-Code list
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/
