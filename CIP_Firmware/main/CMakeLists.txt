idf_component_register(SRCS "pi.c" "IOExpander.c" "heater.c" "display.c" "SD.c" "common.c""stepperMotor.c" "main.c" "GCodeParser.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd driver console esp_driver_sdmmc sdmmc fatfs spiffs)