#include "heater.h"

static const char *TAG = "HEATER";

// ── MAX31865 SPI ─────────────────────────────────────────────
#define MAX31865_CS_PIN     GPIO_NUM_5   // placeholder
#define MAX31865_MOSI_PIN   GPIO_NUM_11  // placeholder
#define MAX31865_MISO_PIN   GPIO_NUM_13  // placeholder
#define MAX31865_CLK_PIN    GPIO_NUM_12  // placeholder

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
static esp_err_t max31865_write_reg(uint8_t reg, uint8_t value)
{
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = (uint8_t[]){ reg | 0x80, value },
    };
    return spi_device_transmit(max31865_spi, &t);
}

static esp_err_t max31865_read_reg(uint8_t reg, uint8_t *out, size_t len)
{
    uint8_t tx[len + 1];
    uint8_t rx[len + 1];
    memset(tx, 0, sizeof(tx));
    tx[0] = reg & 0x7F;  // read — MSB clear

    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_transmit(max31865_spi, &t);
    if (err == ESP_OK)
        memcpy(out, &rx[1], len);
    return err;
}

// ── Temperature reading ───────────────────────────────────────
static float max31865_read_temperature(void)
{
    uint8_t buf[2];
    if (max31865_read_reg(MAX31865_RTD_MSB_REG, buf, 2) != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI read failed");
        return -999.0f;
    }

    uint16_t rtd_raw = ((uint16_t)buf[0] << 8 | buf[1]) >> 1;  // strip fault bit
    float resistance = ((float)rtd_raw / 32768.0f) * PT100_REF_RESISTANCE;

    // Linear approximation: T = (R - R0) / (R0 * alpha)
    float temperature = (resistance - PT100_NOMINAL) / (PT100_NOMINAL * PT100_ALPHA);
    return temperature;
}

// ── Init ──────────────────────────────────────────────────────
esp_err_t heater_init(void)
{
    // SSR pin
    gpio_config_t ssr_conf = {
        .pin_bit_mask = (1ULL << SSR_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&ssr_conf);
    gpio_set_level(SSR_PIN, 0);  // SSR off by default

    // SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = MAX31865_MOSI_PIN,
        .miso_io_num   = MAX31865_MISO_PIN,
        .sclk_io_num   = MAX31865_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // MAX31865 device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 1000000,    // 1 MHz — MAX31865 max is 5 MHz
        .mode           = 1,          // CPOL=0, CPHA=1
        .spics_io_num   = MAX31865_CS_PIN,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &max31865_spi));

    // Configure MAX31865: V_BIAS on, auto conversion, 2/4-wire, 60Hz filter
    uint8_t config = MAX31865_CONFIG_VBIAS_ON |
                     MAX31865_CONFIG_CONVMODE |
                     MAX31865_CONFIG_60HZ;
    ESP_ERROR_CHECK(max31865_write_reg(MAX31865_CONFIG_REG, config));

    ESP_LOGI(TAG, "MAX31865 initialized");
    return ESP_OK;
}

// ── HeaterControl Task ────────────────────────────────────────
void HeaterControl(void *pvParameters)
{
    float target_temp = *(float *)pvParameters;
    ESP_LOGI(TAG, "Heater task started — target: %.1f°C", target_temp);

    while (1)
    {
        float current_temp = max31865_read_temperature();

        if (current_temp < -900.0f)
        {
            // Sensor fault — shut off heater for safety
            gpio_set_level(SSR_PIN, 0);
            ESP_LOGE(TAG, "Sensor fault — heater disabled");
        }
        else
        {
            // Bang-bang with hysteresis
            if (current_temp < (target_temp - HEATER_HYSTERESIS))
            {
                gpio_set_level(SSR_PIN, 1);  // heat on
                ESP_LOGD(TAG, "Heater ON  — %.2f°C / %.1f°C", current_temp, target_temp);
            }
            else if (current_temp >= (target_temp + HEATER_HYSTERESIS))
            {
                gpio_set_level(SSR_PIN, 0);  // heat off
                ESP_LOGD(TAG, "Heater OFF — %.2f°C / %.1f°C", current_temp, target_temp);
            }
            // within hysteresis band — hold current SSR state
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // 10 Hz control loop
    }
}