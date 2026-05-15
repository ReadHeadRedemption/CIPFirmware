#include "GCodeParser.h"
#include "esp_log.h"

static const char *TAG = "GCODE_PARSER";

// Temp file location to load into the esp32

void parse(char *fileLocation)
{
    FILE *file = fopen(fileLocation, "r");
    char line[128];
    char cmd[4] = "";
    float cords[3] = {0.0f, 0.0f, 0.0f};
    float lastCords[3] = {0.0f, 0.0f, 0.0f};
    MoveCmd_t head;
    head.target_x = cords[X];
    head.target_y = cords[Y];
    head.target_z = cords[Z];
    head.feed_rate_hz = 1000;
    uint32_t feed = 0;
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
            // Parse the line for command and coordinates
            // 1. Parse the command first (e.g., "G1", "G0", "M3")
            // This stops parsing at the first space.
            if (sscanf(line, "%15s", cmd) != 1)
            {
                return; // Empty line
            }
            // 2. Search for each coordinate axis individually (Handling optional values)
            char *p;

            // Check X
            if ((p = strchr(line, 'X')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[X]); // Use %f for float!
            }
            // Check Y
            if ((p = strchr(line, 'Y')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[Y]);
            }
            // Check Z
            if ((p = strchr(line, 'Z')) != NULL)
            {
                sscanf(p + 1, "%f", &cords[Z]);
            }
            // Check E
            if ((p = strchr(line, 'F')) != NULL)
            {
                sscanf(p + 1, "%ld", &feed);
            }

            // 3. Print the coordinates safely using %f for float representation
            ESP_LOGI(TAG, "Parsed Line: Command=%s, X=%.3f, Y=%.3f, Z=%.3f, F=%ld", cmd, cords[X], cords[Y], cords[Z], feed);
            if ((strcmp(cmd, "G0") == 0) && (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdPASS))
            {
                ESP_LOGI(TAG, "CALLING G0");
                head.target_x = (float)cords[X];
                head.target_y = (float)cords[Y];
                head.target_z = (float)cords[Z];
                head.feed_rate_hz = 1000;
                ESP_LOGI(TAG, "LAST CORDS X: %.3f Y:%.3f Z:%.3f", lastCords[X], lastCords[Y], lastCords[Z]);
                ESP_LOGI(TAG, "*G0* MOVING HEAD TO X: %.3f Y:%.3f Z:%.3f", head.target_x, head.target_y, head.target_z);
                coordinated_move(&head);
                for(int i = 0; i<3; i++) lastCords[i] = cords[i];
                ESP_LOGI(TAG, "FINISHED MOVING HEAD");
                ESP_LOGI(TAG, "FINISHED G0");
                xSemaphoreGive(parseSemaphore);
            }
            else if ((strcmp(cmd, "G1") == 0) && (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdPASS))
            {
                ESP_LOGI(TAG, "CALLING G1");
                if((cords[X] != lastCords[X]) || (cords[Y] != lastCords[Y]))
                {
                ESP_LOGI(TAG, "LAST CORDS X/Y X: %.3f Y:%.3f", lastCords[X], lastCords[Y]);
                ESP_LOGI(TAG, "SETTING X/Y X: %.3f Y:%.3f", cords[X], cords[Y]);
                head.target_x = (float)cords[X];
                head.target_y = (float)cords[Y];
                head.feed_rate_hz = 1000;
                // turn on extruder so need to calculate how much it extrudes
                ESP_LOGI(TAG, "SETTING FREQUENCY");
                stepper_set_frequency(E, feed);
                ESP_LOGI(TAG, "MOVING HEAD");
                coordinated_move(&head);
                ESP_LOGI(TAG, "FINISHED MOVING HEAD IN X/Y");
                for(int i = 0; i<2; i++) lastCords[i] = cords[i];
                }else if(cords[Z] != lastCords[Z])
                {
                    stepper_set_frequency(E, 0);
                    ESP_LOGI(TAG, "LAST CORDS Z: %.3f", lastCords[Z]);
                    ESP_LOGI(TAG, "SETTING Z: %.3f", cords[Z]);
                    head.target_z = (float)cords[Z];
                    head.feed_rate_hz = 1000;
                    coordinated_move(&head);
                    lastCords[Z] = cords[Z];
                    ESP_LOGI(TAG, "FINISHED MOVING HEAD IN Z");
                    ESP_LOGI(TAG, "FINISHED G1");
                }
                xSemaphoreGive(parseSemaphore);
            }
            else if ((strcmp(cmd, "G21") == 0) && (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdPASS))
            {
                ESP_LOGI(TAG, "SET UNIT MODE MILLIMETERS");
                xSemaphoreGive(parseSemaphore);
            }
            else if ((strcmp(cmd, "G90") == 0) && (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdPASS))
            {
                ESP_LOGI(TAG, "SET DISTANCE MODE ABSOLUTE");
                xSemaphoreGive(parseSemaphore);
            }
            else if ((strcmp(cmd, "G92") == 0) && (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdPASS))
            {
                ESP_LOGI(TAG, "SET AXIS POSITION");
                xSemaphoreGive(parseSemaphore);
                // } else if ((strcmp(cmd, "F") == 0) && (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdPASS)) {
                //     feed =
                //     xSemaphoreGive(parseSemaphore);
            }
            else if ((strcmp(cmd, "G28") == 0) && (xSemaphoreTake(parseSemaphore, portMAX_DELAY) == pdPASS))
            {
                ESP_LOGI(TAG, "CALLING G28");
                homeMotors();
                xSemaphoreGive(parseSemaphore);
            }
        }
        fclose(file);
    }
}
/*
gscrib G-Code list
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/
