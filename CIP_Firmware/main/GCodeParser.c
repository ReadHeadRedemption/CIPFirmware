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

    float scale = 1.0;
    float eScale = 0.2;
    float extrude = 0.0f;

    bool distMode = true;
    MoveCmd_t head;
    head.target_x = cords[X] * scale;
    head.target_y = cords[Y] * scale;
    head.target_z = cords[Z] * scale;
    head.moveE = 0.0f;
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
                        (strcmp(cmd, "G1") == 0))     // linear movement
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
                                     &head.target_x, &head.target_y, &head.target_z);
                            coordinated_move(&head);
                            break;
                        case 1: // linear movement
                            ESP_LOGI(TAG, "CALLING G1");
                            float lastLocation[3] = {head.target_x, head.target_y, head.target_z};
                            float dE[3] = {0.0f};
                            
                            if ((p = strchr(line, 'E')) != NULL)
                        {
                            sscanf(p + 1, "%f", &extrude);
                            head.moveE += extrude * scale * eScale;
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
                            // for (int i = 0; i < 3; i++)
                            //     (dE[i] = fabs(lastLocation[i] - dE[i]));
                            // float extrude = (float)sqrt((dE[0] * dE[0]) +
                            //                             (dE[1] * dE[1]) +
                            //                             (dE[2] * dE[2]));
                            // head.moveE = extrude * scale * eScale;
                            ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f",
                                     head.target_x, head.target_y, head.target_z);
                            ESP_LOGI(TAG, "PUSHING HEAD %.3f MM", head.moveE);
                            coordinated_move(&head);
                            break;
                        default:
                            break;
                        }
                    }
                    else if ((strcmp(cmd, "G2") == 0) || 
                             (strcmp(cmd, "G3") == 0))
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
                        float I, J = 0.0f;
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
                            head.circleDir = (g == 2) ? false : true;
                            
                            // float lastLocation[4] = {head.target_x, head.target_y, head.center_x, head.center_y};
                            // float dE[4] = {0.0f};
                            // for (int i = 0; i < 4; i++)
                            //     (dE[i] = fabs(lastLocation[i] - dE[i]));
                            // float extrude = (float)sqrt((dE[0] * dE[0]) +
                            //                             (dE[1] * dE[1]) +
                            //                             (dE[2] * dE[2]) +
                            //                             (dE[2] * dE[2]));
                            // head.moveE = extrude * scale * eScale;
                            if (coordChange[0])
                                head.target_x = cords[X] * scale;
                            if (coordChange[1])
                                head.target_y = cords[Y] * scale;
                            head.center_x = I;
                            head.center_y = J;
                            SP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f WITH CENTER I:%.3f J:%.3f",
                                     head.target_x, head.target_y, head.center_x, head.center_y);
                            ESP_LOGI(TAG, "PUSHING HEAD %.3f MM", head.moveE);
                            circular_move(&head);
                    }
                    else if (strcmp(cmd, "G4") == 0) // delay in seconds
                    {
                        int delay = 0;
                        if ((p = strchr(line, 'P')) != NULL)
                        {
                            sscanf(p + 1, "%d", &delay); // Use %f for float!
                        }
                        ESP_LOGI(TAG, "CALLING G4: DELAY FOR %d SECONDS", delay);
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
                        homeMotors();
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
                    }
        }
    }
}
/*
gscrib G-Code list
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/
