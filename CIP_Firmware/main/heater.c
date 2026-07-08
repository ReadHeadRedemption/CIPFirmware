#include "heater.h"
#include "esp_lvgl_port.h"

static const char *TAG = "HEATER";

// ── MAX31865 SPI ─────────────────────────────────────────────
#define MAX31865_CS_PIN     RTDCS   // placeholder
#define MAX31865_MOSI_PIN   MOSI  // placeholder
#define MAX31865_MISO_PIN   MISO  // placeholder
#define MAX31865_CLK_PIN    SCK  // placeholder

// MAX31865 registers
#define MAX31865_CONFIG_REG         0x00
#define MAX31865_CONFIG_WRITE       0x80
#define MAX31865_RTD_MSB_REG        0x01
#define MAX31865_CONFIG_VBIAS_ON    0x80
#define MAX31865_CONFIG_CONVMODE    0x40  // auto conversion
#define MAX31865_CONFIG_3WIRE       0x10
#define MAX31865_CONFIG_FAULTSTAT   0x02
#define MAX31865_CONFIG_60HZ        0x00  // 60Hz filter (use 0x01 for 50Hz)

// PT100 constants (Callendar-Van Dusen, simplified linear for our range)
#define PT100_REF_RESISTANCE    430.0f   // reference resistor on MAX31865 board (typical)
#define PT100_NOMINAL           100.0f   // PT100 = 100 ohm at 0°C
#define PT100_ALPHA             0.00385f // standard PT100 alpha coefficient

static spi_device_handle_t max31865_spi;

// ── SPI helpers ───────────────────────────────────────────────
// static esp_err_t max31865_write_reg(uint8_t reg, uint8_t value)
// {
//     lvgl_port_lock(0);
//     spi_transaction_t t = {
//         .length    = 16,
//         .tx_buffer = (uint8_t[]){ reg | 0x80, value },
//     };
//     lvgl_port_unlock();
//     return spi_device_transmit(max31865_spi, &t);
// }

// static esp_err_t max31865_read_reg(uint8_t reg, uint8_t *out, size_t len)
// {
//     lvgl_port_lock(0);
//     uint8_t tx[len + 1];
//     uint8_t rx[len + 1];
//     memset(tx, 0, sizeof(tx));
//     tx[0] = reg & 0x7F;  // read — MSB clear

//     spi_transaction_t t = {
//         .length    = (len + 1) * 8,
//         .tx_buffer = tx,
//         .rx_buffer = rx,
//     };
//     esp_err_t err = spi_device_transmit(max31865_spi, &t);
//     if (err == ESP_OK)
//         memcpy(out, &rx[1], len);
//     lvgl_port_unlock();
//     return err;
// }

// ── Temperature reading ───────────────────────────────────────
// static float max31865_read_temperature(void)
// {
//     uint8_t buf[2];
//     if (max31865_read_reg(MAX31865_RTD_MSB_REG, buf, 2) != ESP_OK)
//     {
//         ESP_LOGE(TAG, "SPI read failed");
//         return -999.0f;
//     }

//     uint16_t rtd_raw = ((uint16_t)buf[0] << 8 | buf[1]) >> 1;  // strip fault bit
//     float resistance = ((float)rtd_raw / 32768.0f) * PT100_REF_RESISTANCE;

//     // Linear approximation: T = (R - R0) / (R0 * alpha)
//     float temperature = (resistance - PT100_NOMINAL) / (PT100_NOMINAL * PT100_ALPHA);
//     return temperature;
// }

static max31865_config_t max31865_cfg = {
    .v_bias = true,
    .filter = MAX31865_FILTER_60HZ,
    .mode = MAX31865_MODE_SINGLE,
    .connection = MAX31865_3WIRE
};

max31865_t max31865_dev = {
        .standard = MAX31865_US_INDUSTRIAL,
        .r_ref = 430,
        .rtd_nominal = 100,
    };;

// ── Init ──────────────────────────────────────────────────────
esp_err_t heater_init(void)
{
    // SSR pin
    // gpio_config_t ssr_conf = {
    //     .pin_bit_mask = (1ULL << SSR_PIN),
    //     .mode         = GPIO_MODE_OUTPUT,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .pull_up_en   = GPIO_PULLUP_DISABLE,
    //     .intr_type    = GPIO_INTR_DISABLE,
    // };
    // gpio_config(&ssr_conf);
    // gpio_set_level(SSR_PIN, 0);  // SSR off by default

    // SPI bus
    // spi_bus_config_t bus_cfg = {
    //     .mosi_io_num   = MAX31865_MOSI_PIN,
    //     .miso_io_num   = MAX31865_MISO_PIN,
    //     .sclk_io_num   = MAX31865_CLK_PIN,
    //     .quadwp_io_num = -1,
    //     .quadhd_io_num = -1,
    // };
    // ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // MAX31865 device
    // spi_device_interface_config_t dev_cfg = {
    //     .clock_speed_hz = 1000000,    // 1 MHz — MAX31865 max is 5 MHz
    //     .mode           = 1,          // CPOL=0, CPHA=1
    //     .spics_io_num   = MAX31865_CS_PIN,
    //     .queue_size     = 1,
    // };
    // ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev_cfg, &max31865_spi));

    // // Configure MAX31865: V_BIAS on, auto conversion, 2/4-wire, 60Hz filter
    // uint8_t config = MAX31865_CONFIG_VBIAS_ON |
    //                  MAX31865_CONFIG_CONVMODE |
    //                  MAX31865_CONFIG_60HZ |
    //                  MAX31865_CONFIG_3WIRE;
    // ESP_ERROR_CHECK(max31865_write_reg(MAX31865_CONFIG_REG, config));

    // Initialize device
    ESP_ERROR_CHECK(max31865_init_desc(&max31865_dev, SPI3_HOST, MAX31865_MAX_CLOCK_SPEED_HZ, RTDCS));

    // Configure device
    ESP_ERROR_CHECK(max31865_set_config(&max31865_dev, &max31865_cfg));

    ESP_LOGI(TAG, "MAX31865 initialized");
    return ESP_OK;
}

// ── HeaterControl Task ────────────────────────────────────────
void HeaterControl()
{
    float target_temp = 0;
    ESP_LOGI(TAG, "Heater task started — target: %.1f°C", target_temp);

    float current_temp;

    BaseType_t skip_for_new_temp = pdFALSE;
    BaseType_t new_temp_unreached = pdFALSE;
    while (1)
    {

        esp_err_t res = max31865_measure(&max31865_dev, &current_temp);

        // max31865_clear_fault_status(&max31865_dev);

        printf("CURRENT TEMPERATURE: %f\n", current_temp);

                // Two checks: check if there is any result, and if so, a sanity check for temperature. Below 18 C is not sane
        if (res != ESP_OK || current_temp < 10)
        {
            ESP_LOGE(TAG, "Failed to measure: %d (%s)", res, esp_err_to_name(res));
            if (SSRE != GPIO_NUM_NC) gpio_set_level(SSRE, 0);
            // vTaskDelete(NULL);
        }
        else
        {
            ESP_LOGI(TAG, "Temperature: %.4f C (%.4f F)", current_temp, current_temp * 1.8 + 32);

            // If the temperature is less than the desired, heat up to the desired temperature
            if (current_temp < target_temp)
            {
                // Turn on SSR to conduct power through heater
                if (SSRE != GPIO_NUM_NC) 
                    gpio_set_level(SSRE, 1);
                
                // Generally, the hot plate heats up at 1 degree C per second when on, so wait a number of seconds equal to the difference in set and real temperatures
                int onTime = (int)(1000 * (target_temp - current_temp));

                // If this time is greater than 50 ms, wait for this time
                if (onTime > 50)
                {
                    skip_for_new_temp = xQueueReceive(temperature_queue, &target_temp, pdMS_TO_TICKS(onTime));
                }
                    
                // If this time is too short (the temperature is already very close to the set temperature), clamp waiting at 50 ms to not strain the SSR
                // CONSIDER REMOVING THIS AND TO NOT HEAT AT ALL; NEEDS TESTING; WORKS ANYWAY
                else
                {
                    skip_for_new_temp = xQueueReceive(temperature_queue, &target_temp, pdMS_TO_TICKS(50));
                }

                // Turn off SSR
                if (SSRE != GPIO_NUM_NC) 
                    gpio_set_level(SSRE, 0);
            }


            if (skip_for_new_temp == pdFALSE)
            {  
                 /*  
                    If heating, both the hot plate and RTD need time to fully absorb the heat and temperature change
                    If not heating, we should still wait a certain interval to prevent rapid switching and because temperature does not change that quickly (perhaps at -0.1 degree C/s)
                    Generally, it was observed that RTDs needed some base amount of time (we give four seconds) to pick up to changes in temperature and extra time for larger changes in temperature (we give 100 ms for each difference in degrees C)
                    However, this time decreases with increased temperature because the larger temperature increase will occur more rapidly
                */
                // CHANGE COMMENT ABOVE TO REFLECT CHANGES
                int waitTime = 30000 - 10 * (target_temp - current_temp);

                // If the wait time is less than 4 seconds (real temperature is greater than set temperature), clamp at four seconds
                if (waitTime < 20000)
                {
                    skip_for_new_temp = xQueueReceive(temperature_queue, &target_temp, pdMS_TO_TICKS(20000));
                }
                
                // Otherwise, wait the calculated time
                else
                {
                    skip_for_new_temp = xQueueReceive(temperature_queue, &target_temp, pdMS_TO_TICKS(waitTime));
                }
            }

            if (new_temp_unreached == pdTRUE)
            {
                printf("GIVING SEMAPHORE\n");
                xSemaphoreGive(tempReachedSemaphore);
                new_temp_unreached = pdFALSE;
            }

            if (skip_for_new_temp == pdTRUE)
            {
                if (current_temp > target_temp)
                {
                    printf("GIVING SEMAPHORE\n");
                    xSemaphoreGive(tempReachedSemaphore);
                }
                else
                {
                    new_temp_unreached = pdTRUE;
                }
                skip_for_new_temp = pdFALSE;
            }
        }
    }
}