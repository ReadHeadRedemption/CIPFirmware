idf_component_register(SRCS "heater.c" "display.c" "SD.c" "common.c""stepperMotor.c" "main.c" "GCodeParser.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd driver spiffs console)