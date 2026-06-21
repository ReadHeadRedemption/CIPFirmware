#include "GCodeParser.h"
#include "esp_log.h"

static const char *TAG = "GCODE_PARSER";

uint32_t feed = 0;

float scale = 1.0f;
float eScale = 0.00005f;
float extrude = 0.0f;
float total_extruded = 0.0f;

bool distMode = true;


// Temp file location to load into the esp32
void readParseFile(char *fileLocation)
{
    FILE *file = fopen(fileLocation, "r");
    char line[128];
    char *p;
    char cmd[7] = "";
    float cords[3] = {0.0f, 0.0f, 0.0f};

    uint32_t feed = 0;

    // float scale = 1.0f;
    // float eScale = 0.006f;
    // float extrude = 0.0f;

    bool distMode = true;
    MoveCmd_t head;
    head.target_x = cords[X] * scale;
    head.target_y = cords[Y] * scale;
    head.target_z = cords[Z] * scale;
    head.moveE = 0.0f;
    head.feed_rate_hz = 5000;
    parseSemaphore = xSemaphoreCreateMutex();

    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open G-code file: %s", fileLocation);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "Successfully opened G-code file: %s", fileLocation);

        char up[] = "G0 Z5.5 F20000";

        char mini_extrude[] = "G1 E40 F200";
        // char down[] = "G0 Z0.5";

        // TEMPORARY CODE FOR INITIAL EXTRUSION
        parse(up); 
        parse(mini_extrude);
        // parse(mini_extrude);
        for (int i = 0; i < 10; i++)
        {
            parse(mini_extrude);
            vTaskDelay(pdMS_TO_TICKS(2500));
        }
        // vTaskDelay(pdMS_TO_TICKS(10000));
        // parse(down);

        while (fgets(line, sizeof(line), file))
        {
            parse(line);
        }
        fclose(file);
    }
}

void parse(char *line)
{    
    bool pullback = true;
    
    char *p;
    char cmd[4] = "";
    float cords[3] = {0.0f, 0.0f, 0.0f};

    parseSemaphore = xSemaphoreCreateMutex();

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
    if (cmd[0] == 'G')
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
            }
            // Check Y
            if ((p = strchr(line, 'Y')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[Y]);
                ESP_LOGI(TAG, "Parsed Y: %.3f", cords[Y]);
                coordChange[1] = true;
            }
            // Check Z
            if ((p = strchr(line, 'Z')) != NULL)
            {
                coordChange[2] = true;
                sscanf(p + 1, "%f", &cords[Z]);
                ESP_LOGI(TAG, "Parsed Z: %.3f", cords[Z]);
            }
            // Check do pull-back
            if ((p = strchr(line, 'N')) != NULL)
            {
                pullback = false;
                ESP_LOGI(TAG, "Pullback");
            }
            int g = atoi(cmd + 1);
            switch (g)
            {
            case 0: // rapid movement
                ESP_LOGI(TAG, "CALLING G0");
                head.feed_rate_hz = 5000;
                // Check F
                if ((p = strchr(line, 'F')) != NULL)
                {
                    sscanf(p + 1, "%ld", &feed);
                    head.feed_rate_hz = feed;
                    ESP_LOGI(TAG, "Parsed G0 F: %ld", feed);
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
                float lastLocation[3] = {head.target_x, head.target_y, head.target_z};
                float dE[3] = {0.0f};
                if ((p = strchr(line, 'F')) != NULL)
                {
                    sscanf(p + 1, "%ld", &feed);
                    head.feed_rate_hz = feed;
                    ESP_LOGI(TAG, "Parsed G1 F: %ld", feed);
                }
                if ((p = strchr(line, 'E')) != NULL)
                {
                    sscanf(p + 1, "%f", &extrude);
                    head.moveE += extrude * scale * eScale;
                    head.feed_rate_hz = 200;
                    coordinated_move(&head);
                    head.feed_rate_hz = 500;
                    if (coordChange[0])
                        head.target_x += cords[X] * scale;
                    if (coordChange[1])
                        head.target_y += cords[Y] * scale;
                    if (coordChange[2])
                        head.target_z += cords[Z] * scale;
                    pullback = false;
                }
                else if (distMode)
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
                    head.feed_rate_hz = 200;
                    head.moveE += extrude * scale * eScale; //+ 0.01 * total_extruded;
                    coordinated_move(&head);
                    head.feed_rate_hz = 500;
                    // head.moveE += extrude * scale * eScale / 2;
                    if (coordChange[0])
                        head.target_x = cords[X] * scale;
                    if (coordChange[1])
                        head.target_y = cords[Y] * scale;
                    if (coordChange[2])
                        head.target_z = cords[Z] * scale;
                    head.feed_rate_hz /= 2;
                    coordinated_move(&head);
                    head.feed_rate_hz *= 2;
                }
                else
                {
                }

                // Calculate the length of the move for E calculation

                ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f",
                         head.target_x, head.target_y, head.target_z);
                ESP_LOGI(TAG, "PUSHING HEAD TO %.3f (%.5f) MM", head.moveE, head.moveE);
                coordinated_move(&head);
                if (pullback)
                {
                    head.feed_rate_hz = 200;
                    head.moveE -= extrude * (scale * eScale * 1.0f - 0.00002f);
                    // head.moveE = 0;
                    ESP_LOGI(TAG, "MOVING TO X:%.3f Y:%.3f Z:%.3f",
                            head.target_x, head.target_y, head.target_z);
                    ESP_LOGI(TAG, "PUSHING HEAD TO %.3f (%.5f) MM", head.moveE, head.moveE);
                    coordinated_move(&head);
                    head.feed_rate_hz = 500;
                }

                total_extruded += extrude;

                break;
            default:
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
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
                                        (dE[3] * dE[3])); // Why three axes?
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
/*
gscrib G-Code list
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/
