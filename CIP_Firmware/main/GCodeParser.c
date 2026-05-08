#include "GCodeParser.h"
#include "GCodes.h"
#include "esp_log.h"



static const char *TAG = "GCODE_PARSER";

// Temp file location to load into the esp32

SemaphoreHandle_t parseSemaphore; 


void parse(char *fileLocation){
    FILE *file = fopen(fileLocation, "r");
    char line[128];
    char cmd [4] = "";
    float cords [4] = {0.0f, 0.0f, 0.0f, 0.0f};
    parseSemaphore = xSemaphoreCreateMutex();
    
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open G-code file: %s", fileLocation);
        return;
    }
    else{
        ESP_LOGI(TAG, "Successfully opened G-code file: %s", fileLocation);
        while (fgets(line, sizeof(line), file)) {
            // Parse the line for command and coordinates
            // 1. Parse the command first (e.g., "G1", "G0", "M3")
            // This stops parsing at the first space.
            if (sscanf(line, "%15s", cmd) != 1) {
                return; // Empty line
            }

            // 2. Search for each coordinate axis individually (Handling optional values)
            char *p;
            
            // Check X
            if ((p = strchr(line, 'X')) != NULL) {
                sscanf(p + 1, "%f", &cords[X]); // Use %f for float!
            }
            // Check Y
            if ((p = strchr(line, 'Y')) != NULL) {
                sscanf(p + 1, "%f", &cords[Y]);
            }
            // Check Z
            if ((p = strchr(line, 'Z')) != NULL) {
                sscanf(p + 1, "%f", &cords[Z]);
            }
            // Check E
            if ((p = strchr(line, 'E')) != NULL) {
                sscanf(p + 1, "%f", &cords[E]);
            }

            // 3. Print the coordinates safely using %f for float representation
            ESP_LOGI(TAG, "Parsed Line: Command=%s, X=%.3f, Y=%.3f, Z=%.3f, E=%.3f", cmd, cords[X], cords[Y], cords[Z], cords[E]);
            if (strcmp(cmd, "G0") == 0) {
                xSemaphoreTake(parseSemaphore, portMAX_DELAY);
                G0(cords[X], cords[Y], cords[Z], cords[E]);
                xSemaphoreGive(parseSemaphore);
            } else if (strcmp(cmd, "G1") == 0) {
                xSemaphoreTake(parseSemaphore, portMAX_DELAY);
                G1(cords[X], cords[Y], cords[Z], cords[E]);
                xSemaphoreGive(parseSemaphore); 
            } else if (strcmp(cmd, "G21") == 0) {
                xSemaphoreTake(parseSemaphore, portMAX_DELAY);
                G21();
                xSemaphoreGive(parseSemaphore);
            } else if (strcmp(cmd, "G90") == 0) {
                xSemaphoreTake(parseSemaphore, portMAX_DELAY);
                G90();
                xSemaphoreGive(parseSemaphore);
            } else if (strcmp(cmd, "G92") == 0) {
                xSemaphoreTake(parseSemaphore, portMAX_DELAY);
                G92();
                xSemaphoreGive(parseSemaphore);
            } else if (strcmp(cmd, "F") == 0) {
                xSemaphoreTake(parseSemaphore, portMAX_DELAY);
                FeedRate((int)cords[E]); // Assuming feed rate is given in E coordinate for simplicity
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

