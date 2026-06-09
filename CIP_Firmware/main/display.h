#ifndef DISPLAY_H
#define DISPLAY_H

#include "common.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"

#include "stepperMotor.h"

extern MoveCmd_t head;


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the SPI bus, LCD driver, touch controller, and LVGL port.
 *        Constructs the baseline user interface.
 */
void display_init(void);

/**
 * @brief Thread-safely updates the telemetry/status label on the UI.
 * @param text The new string to display
 */
void display_update_status(const char *text);

/**
 * @brief FreeRTOS task entry for display initialization and UI management.
 *        Create this as a pinned or regular task from `app_main`.
 */
void display_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H