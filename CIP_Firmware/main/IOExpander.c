#include "IOExpander.h"
#include "pins.h"
#include "esp_log.h"

static const char *TAG = "IO_EXPANDER";

i2c_master_bus_handle_t i2c_handle = NULL;
i2c_master_bus_handle_t bus_handle = NULL;
esp_io_expander_handle_t io_expander = NULL;

esp_err_t init_io_expander()
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA,
        .scl_io_num = SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,

    };

    i2c_new_master_bus(&bus_config, &i2c_handle);

    esp_err_t err = esp_io_expander_new_i2c_tca95xx_16bit(i2c_handle, 0x20, &io_expander);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize IO Expander! Check wiring.");
        // Halt here, or disable features that rely on the expander
        return ESP_FAIL;
    }
    esp_io_expander_gpio_wrapper_append_handler(io_expander, GPIO_NUM_MAX);

    ////////////////////////////////////////////////////////////////////
    //
    //                  SETTING PIN DIRECTIONS
    //
    ////////////////////////////////////////////////////////////////////

    int OUTPUTPINS[10] = {xEnable, yEnable, zEnable, eEnable, SSRE, FAN1, xDir, yDir, zDir, eDir};
    for (int i = 0; i < 10; i++)
    {
        gpio_set_direction(OUTPUTPINS[i], GPIO_MODE_OUTPUT);
    }
    int INPUTPINS[6] = {xSwitch, ySwitch, zSwitch, eSwitch, HEAD_ID_0, HEAD_ID_1};
    for (int i = 0; i < 6; i++)
    {
        gpio_set_direction(INPUTPINS[i], GPIO_MODE_INPUT);
    }
    return ESP_OK;
}

void printExpanderState(void *pvParameters)
{
    while (1)
    {
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            esp_io_expander_print_state(io_expander);
            xSemaphoreGive(i2c_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}