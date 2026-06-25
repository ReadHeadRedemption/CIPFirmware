#ifndef IO_EXPANDER_H
#define IO_EXPANDER_H

#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_io_expander_gpio_wrapper.h"
#include "driver/i2c_master.h"
#include "common.h"

extern esp_io_expander_handle_t io_expander;
esp_err_t init_io_expander();
void printExpanderState(void *pvParameters);

#endif // IO_EXPANDER_H