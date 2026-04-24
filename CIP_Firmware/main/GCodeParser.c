#include "GCodeParser.h"
#include "esp_log.h"


static const char *TAG = "GCODE_PARSER";

// Temp file location to load into the esp32



void parse(char *fileLocation){
    FILE *file = fopen(fileLocation, "r");
    char line[128];
    char cmd [4] = "";
    double cords [4] = {0, 0, 0, 0};
    
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open G-code file: %s", fileLocation);
        return;
    }
    else{
        ESP_LOGI(TAG, "Successfully opened G-code file: %s", fileLocation);
        while (fgets(line, sizeof(line), file)) {
            // Parse the line for command and coordinates
            sscanf(line, "%s X%lf Y%lf Z%lf E%lf", cmd, &cords[0], &cords[1], &cords[2], &cords[3]);
            ESP_LOGI(TAG, "Parsed Line: Command=%s, X=%lf, Y=%lf, Z=%lf, E=%lf", cmd, cords[0], cords[1], cords[2], cords[3]);
            // Here you would add logic to handle the command and move the motors accordingly

        }
        fclose(file);
    }

}
/*
gscrib G-Code list 
https://gscrib.readthedocs.io/en/latest/gcode-table.html
*/

