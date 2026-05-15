#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "driver/rmt_tx.h"

#include <max31865.h>
// #include <esp_idf_lib_helpers.h>

// ======== USER PIN CONFIGURATION ========
// Replace these pins with your actual PCB pins.
#define PIN_X_STEP          GPIO_NUM_2
#define PIN_X_DIR           GPIO_NUM_4
#define PIN_X_EN            GPIO_NUM_1
#define PIN_X_MIN           GPIO_NUM_NC
#define PIN_X_MAX           GPIO_NUM_NC

#define PIN_Y_STEP          GPIO_NUM_5
#define PIN_Y_DIR           GPIO_NUM_6
#define PIN_Y_EN            GPIO_NUM_15
#define PIN_Y_MIN           GPIO_NUM_NC
#define PIN_Y_MAX           GPIO_NUM_NC

#define PIN_Z_STEP          GPIO_NUM_41
#define PIN_Z_DIR           GPIO_NUM_40
#define PIN_Z_EN            GPIO_NUM_39
#define PIN_Z_MIN           GPIO_NUM_NC
#define PIN_Z_MAX           GPIO_NUM_NC

#define PIN_E_STEP          GPIO_NUM_17
#define PIN_E_DIR           GPIO_NUM_18
#define PIN_E_EN            GPIO_NUM_16

#define PIN_Z_PROBE         GPIO_NUM_NC
#define PIN_BED_SSR         GPIO_NUM_38

#define PIN_SD_MISO         GPIO_NUM_NC
#define PIN_SD_MOSI         GPIO_NUM_NC
#define PIN_SD_SCLK         GPIO_NUM_NC
#define PIN_SD_CS           GPIO_NUM_NC

// ======== TFT / TOUCH CONFIGURATION (LVGL + esp_lcd) ========
// Leave these as GPIO_NUM_NC until you wire the panel.
// Keep this on a separate bus from the SD card to simplify timing and bus ownership.
#define PIN_TFT_MISO        GPIO_NUM_46   // Usually shared with touch MISO
#define PIN_TFT_MOSI        GPIO_NUM_11
#define PIN_TFT_SCLK        GPIO_NUM_10
#define PIN_TFT_CS          GPIO_NUM_14
#define PIN_TFT_DC          GPIO_NUM_12
#define PIN_TFT_RST         GPIO_NUM_13
#define PIN_TFT_BCKL        GPIO_NUM_9
#define PIN_TOUCH_CS        GPIO_NUM_3
#define PIN_TOUCH_IRQ       GPIO_NUM_8   // Optional. Wire if available for better touch detection.

#define TFT_SPI_HOST        SPI3_HOST
#define TFT_H_RES           240
#define TFT_V_RES           320
#define TFT_PIXEL_CLOCK_HZ  (26 * 1000 * 1000)
#define TFT_DRAW_BUF_PIXELS (TFT_H_RES * 40)
#define TFT_BACKLIGHT_ON    1

// Touch orientation / calibration flags. Adjust these to match your module.
#define TOUCH_SWAP_XY       0
#define TOUCH_MIRROR_X      0
#define TOUCH_MIRROR_Y      1

// Thermistor on ADC1 channel 6 => GPIO34 on classic ESP32.
// Change both if you use a different ESP32 variant / pin.
#define BED_THERM_ADC_UNIT      ADC_UNIT_1
#define BED_THERM_ADC_CHANNEL   ADC_CHANNEL_6
#define BED_THERM_ATTEN         ADC_ATTEN_DB_12

#define RTD_CONNECTION MAX31865_3WIRE
#define RTD_STANDARD MAX31865_US_INDUSTRIAL
#define FILTER MAX31865_FILTER_60HZ

static max31865_config_t config = {
    .v_bias = true,
    .filter = FILTER,
    .mode = MAX31865_MODE_SINGLE,
    .connection = RTD_CONNECTION
};

// ======== MACHINE CONFIGURATION ========
#define STEPS_PER_MM_X      40.0f
#define STEPS_PER_MM_Y      80.0f
#define STEPS_PER_MM_Z      80.0f
#define STEPS_PER_MM_E      800.0f   // Plunger axis. Replace with your real value.

#define HOMING_FEED_MM_MIN  600.0f
#define JOG_FEED_MM_MIN     1200.0f
#define DEFAULT_FEED_MM_MIN 1200.0f
#define HOMING_BACKOFF_MM   3.0f


#define STEP_PULSE_US       4   // DRV8825 min pulse width is low single-digit us.
#define STEP_MIN_INTERVAL_US 300
#define CONSOLE_LINE_MAX    160

#define RMT_RESOLUTION_HZ       1000000UL
#define RMT_MEM_BLOCK_SYMBOLS   48
#define RMT_TRANS_QUEUE_DEPTH   1
#define RMT_MOVE_CHUNK_STEPS    256
#define RMT_HOME_CHUNK_STEPS    64
#define RMT_MAX_DURATION_US     32767U
#define DIR_SETUP_DELAY_US      5U
#define RMT_RAMP_MIN_STEPS      0
#define RMT_RAMP_MAX_STEPS      0
#define RMT_START_SLOWDOWN      2.5f


typedef enum {
    AXIS_X = 0,
    AXIS_Y,
    AXIS_Z,
    AXIS_E,
    AXIS_COUNT
} axis_id_t;

typedef struct {
    gpio_num_t step_pin;
    gpio_num_t dir_pin;
    gpio_num_t en_pin;
    gpio_num_t min_pin;
    gpio_num_t max_pin;
    float steps_per_mm;
    bool invert_dir;
    bool home_to_min;
    bool enabled_low;
} axis_hw_t;

typedef struct {
    int32_t pos_steps[AXIS_COUNT];
    bool absolute_mode;
    bool homed[3];
    bool faulted;
    char fault_msg[96];
    bool printing;
    bool stop_requested;
    float bed_target_c;
    float bed_temp_c;
    bool heater_enabled;
} machine_state_t;

typedef struct {
    bool has_x, has_y, has_z, has_e, has_f;
    float x, y, z, e, f;
} move_words_t;

typedef enum {
    UI_CMD_MOVE_ABS = 0,
    UI_CMD_HOME,
    UI_CMD_STOP
} ui_cmd_type_t;

typedef struct {
    ui_cmd_type_t type;
    float x;
    float y;
    float z;
    float e;
    float f;
} ui_cmd_t;

static const axis_hw_t g_axes[AXIS_COUNT] = {
    [AXIS_X] = { PIN_X_STEP, PIN_X_DIR, PIN_X_EN, PIN_X_MIN, PIN_X_MAX, STEPS_PER_MM_X, false, true, true },
    [AXIS_Y] = { PIN_Y_STEP, PIN_Y_DIR, PIN_Y_EN, PIN_Y_MIN, PIN_Y_MAX, STEPS_PER_MM_Y, false, true, true },
    [AXIS_Z] = { PIN_Z_STEP, PIN_Z_DIR, PIN_Z_EN, PIN_Z_MIN, PIN_Z_MAX, STEPS_PER_MM_Z, false, true, true },
    [AXIS_E] = { PIN_E_STEP, PIN_E_DIR, PIN_E_EN, GPIO_NUM_NC, GPIO_NUM_NC, STEPS_PER_MM_E, false, true, true },
};

static SemaphoreHandle_t stop_semaphore;
static SemaphoreHandle_t move_semaphore;

static SemaphoreHandle_t g_state_mutex;
static machine_state_t g_state;
static sdmmc_card_t *g_sdcard;
static adc_oneshot_unit_handle_t g_adc_handle;
static adc_cali_handle_t g_adc_cali_handle;
static bool g_adc_cali_enabled;
static SemaphoreHandle_t g_motion_mutex;
static QueueHandle_t g_ui_cmd_queue;

static esp_lcd_panel_io_handle_t g_lcd_io;
static esp_lcd_panel_handle_t g_lcd_panel;
static esp_lcd_touch_handle_t g_touch;
static lv_disp_t *g_lv_disp;
static lv_indev_t *g_lv_touch_indev;

static lv_obj_t *g_ui_pos_label;
static lv_obj_t *g_ui_status_label;
static lv_obj_t *g_ui_temp_label;
static lv_obj_t *g_ui_ta_x;
static lv_obj_t *g_ui_ta_y;
static lv_obj_t *g_ui_ta_z;
static lv_obj_t *g_ui_ta_e;
static lv_obj_t *g_ui_active_ta;

static rmt_channel_handle_t g_rmt_tx_chan[AXIS_COUNT];
static rmt_encoder_handle_t g_rmt_copy_encoder[AXIS_COUNT];
static rmt_sync_manager_handle_t g_rmt_sync_mgr_cache[1 << AXIS_COUNT];

ui_cmd_t ui_cmd;

static void rmt_abort_all(void);

// ---------- Utility ----------
static inline float steps_to_mm(axis_id_t axis, int32_t steps) {
    return (float)steps / g_axes[axis].steps_per_mm;
}

static inline int32_t mm_to_steps(axis_id_t axis, float mm) {
    return lroundf(mm * g_axes[axis].steps_per_mm);
}

static bool machine_faulted(void) {
    bool faulted;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    faulted = g_state.faulted;
    xSemaphoreGive(g_state_mutex);
    return faulted;
}

static void set_axis_enable(axis_id_t axis, bool enabled) {
    const axis_hw_t *a = &g_axes[axis];
    if (!pin_is_valid(a->en_pin)) return;
    const int level = a->enabled_low ? (enabled ? 0 : 1) : (enabled ? 1 : 0);
    gpio_set_level(a->en_pin, level);
}

static void set_all_axes_enabled(bool enabled) {
    for (int i = 0; i < AXIS_COUNT; ++i) {
        set_axis_enable((axis_id_t)i, enabled);
    }
}

static uint32_t clamp_period_us(uint32_t period_us) {
    if (period_us < (STEP_PULSE_US + 1)) return STEP_PULSE_US + 1;
    if (period_us > RMT_MAX_DURATION_US) return RMT_MAX_DURATION_US;
    return period_us;
}

static uint32_t move_interval_with_ramp(uint32_t base_interval_us, int32_t step_index, int32_t total_steps) {
    if (total_steps <= 2) return clamp_period_us(base_interval_us);

    int32_t ramp_steps = total_steps / 6;
    if (ramp_steps > RMT_RAMP_MAX_STEPS) ramp_steps = RMT_RAMP_MAX_STEPS;
    if (ramp_steps < RMT_RAMP_MIN_STEPS) {
        if (total_steps >= (RMT_RAMP_MIN_STEPS * 2))
            ramp_steps = RMT_RAMP_MIN_STEPS;
        else
            ramp_steps = total_steps / 2;
    }
    if (ramp_steps < 1) return clamp_period_us(base_interval_us);

    float factor = 1.0f;
    if (step_index < ramp_steps) {
        float t = (float)step_index / (float)ramp_steps;
        factor = RMT_START_SLOWDOWN - (RMT_START_SLOWDOWN - 1.0f) * t;
    }
    else if (step_index > (total_steps - ramp_steps)) {
        int32_t remain = total_steps - step_index;
        float t = (float)remain / (float)ramp_steps;
        factor = RMT_START_SLOWDOWN - (RMT_START_SLOWDOWN - 1.0f) * t;
    }

    return clamp_period_us((uint32_t)lroundf((float)base_interval_us * factor));
}

static rmt_sync_manager_handle_t rmt_get_sync_manager_for_mask(uint32_t mask) {
    if (__builtin_popcount(mask) <= 1) return NULL;

    rmt_channel_handle_t channels[AXIS_COUNT];
    size_t count = 0;
    for (int axis = 0; axis < AXIS_COUNT; ++axis) {
        if ((mask & (1U << axis)) && g_rmt_tx_chan[axis]) {
            channels[count++] = g_rmt_tx_chan[axis];
        }
    }
    if (count <= 1) return NULL;

    rmt_sync_manager_config_t sync_cfg = {
        .tx_channel_array = channels,
        .array_size = count,
    };
    rmt_sync_manager_handle_t mgr = NULL;
    esp_err_t err = rmt_new_sync_manager(&sync_cfg, &mgr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RMT sync manager unavailable for mask 0x%X (%s)", (unsigned)mask, esp_err_to_name(err));
        return NULL;
    }
    return mgr;
}

static void rmt_abort_all(void) {
    for (int i = 0; i < AXIS_COUNT; ++i) {
        if (g_rmt_tx_chan[i]) {
            rmt_disable(g_rmt_tx_chan[i]);
            rmt_enable(g_rmt_tx_chan[i]);
        }
    }
}

static esp_err_t rmt_motion_init(void) {
    rmt_copy_encoder_config_t copy_cfg = {};

    for (int i = 0; i < AXIS_COUNT; ++i) {
        if (!pin_is_valid(g_axes[i].step_pin)) {
            g_rmt_tx_chan[i] = NULL;
            g_rmt_copy_encoder[i] = NULL;
            continue;
        }

        rmt_tx_channel_config_t tx_cfg = {
            .gpio_num = g_axes[i].step_pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = RMT_RESOLUTION_HZ,
            .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
            .trans_queue_depth = RMT_TRANS_QUEUE_DEPTH,
            .intr_priority = 3,
        };
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &g_rmt_tx_chan[i]), TAG, "RMT TX channel create failed");
        ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &g_rmt_copy_encoder[i]), TAG, "RMT copy encoder create failed");
        ESP_RETURN_ON_ERROR(rmt_enable(g_rmt_tx_chan[i]), TAG, "RMT enable failed");
    }
    return ESP_OK;
}

static esp_err_t rmt_execute_chunk(const uint32_t chunk_steps[AXIS_COUNT], uint32_t chunk_time_us) {
    axis_id_t active_axes[AXIS_COUNT];
    rmt_transmit_config_t tx_cfgs[AXIS_COUNT];
    rmt_symbol_word_t step_symbols[AXIS_COUNT][RMT_MOVE_CHUNK_STEPS];
    int active_count = 0;
    uint32_t active_mask = 0;
    esp_err_t ret = ESP_OK;

    for (int axis = 0; axis < AXIS_COUNT; ++axis) {
        if (chunk_steps[axis] == 0) continue;
        if (!g_rmt_tx_chan[axis] || !g_rmt_copy_encoder[axis]) {
            ESP_LOGE(TAG, "Axis %d has no RMT backend configured", axis);
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (chunk_steps[axis] > RMT_MOVE_CHUNK_STEPS) {
            ESP_LOGE(TAG, "Axis %d chunk too large for symbol buffer (%" PRIu32 ")", axis, chunk_steps[axis]);
            return ESP_ERR_INVALID_ARG;
        }

        uint32_t period_us = clamp_period_us((chunk_time_us + chunk_steps[axis] / 2) / chunk_steps[axis]);
        for (uint32_t s = 0; s < chunk_steps[axis]; ++s) {
            step_symbols[active_count][s] = (rmt_symbol_word_t) {
                .level0 = 1,
                .duration0 = STEP_PULSE_US,
                .level1 = 0,
                .duration1 = period_us - STEP_PULSE_US,
            };
        }

        tx_cfgs[active_count] = (rmt_transmit_config_t) {
            .loop_count = 0,
        };
        active_axes[active_count] = (axis_id_t)axis;
        active_mask |= (1U << axis);
        active_count++;
    }

    if (active_count == 0) return ESP_OK;

    esp_rom_delay_us(DIR_SETUP_DELAY_US);

    rmt_sync_manager_handle_t sync_mgr = rmt_get_sync_manager_for_mask(active_mask);

    for (int i = 0; i < active_count; ++i) {
        size_t payload_size = chunk_steps[active_axes[i]] * sizeof(rmt_symbol_word_t);
        ret = rmt_transmit(g_rmt_tx_chan[active_axes[i]], g_rmt_copy_encoder[active_axes[i]],
                           step_symbols[i], payload_size, &tx_cfgs[i]);
        if (ret != ESP_OK) goto out;
    }

    for (int i = 0; i < active_count; ++i) {
        ret = rmt_tx_wait_all_done(g_rmt_tx_chan[active_axes[i]], -1);
        if (ret != ESP_OK) goto out;
    }

out:
    if (sync_mgr) {
        rmt_sync_reset(sync_mgr);
        rmt_del_sync_manager(sync_mgr);
    }
    return ret;
}

static esp_err_t rmt_move_axis_steps(axis_id_t axis, bool positive, uint32_t steps, uint32_t interval_us) {
    if (steps == 0) return ESP_OK;
    if (!g_rmt_tx_chan[axis]) return ESP_ERR_NOT_SUPPORTED;

    esp_err_t ret = ESP_OK;
    uint32_t remaining = steps;

    set_axis_enable(axis, true);

    bool level = positive;
    if (g_axes[axis].invert_dir) level = !level;
    if (pin_is_valid(g_axes[axis].dir_pin)) {
        gpio_set_level(g_axes[axis].dir_pin, level ? 1 : 0);
    }

    while (remaining > 0) {
        uint32_t chunk = remaining > RMT_HOME_CHUNK_STEPS ? RMT_HOME_CHUNK_STEPS : remaining;
        uint32_t counts[AXIS_COUNT] = {0};
        counts[axis] = chunk;

        ret = rmt_execute_chunk(counts, chunk * clamp_period_us(interval_us));
        if (ret != ESP_OK) goto cleanup;

        remaining -= chunk;
        if (machine_faulted()) {
            ret = ESP_FAIL;
            goto cleanup;
        }
    }

cleanup:
    if (ret != ESP_OK) {
        rmt_abort_all();
    }
    set_axis_enable(axis, false);
    return ret;
}

typedef struct {
    uint32_t step_count[AXIS_COUNT];
    rmt_symbol_word_t *symbols[AXIS_COUNT];
    uint32_t active_mask;
} rmt_move_plan_t;

static void rmt_move_plan_free(rmt_move_plan_t *plan) {
    if (!plan) return;
    for (int axis = 0; axis < AXIS_COUNT; ++axis) {
        free(plan->symbols[axis]);
        plan->symbols[axis] = NULL;
        plan->step_count[axis] = 0;
    }
    plan->active_mask = 0;
}

static esp_err_t rmt_build_move_plan(const int32_t delta[AXIS_COUNT], int32_t max_steps, uint32_t base_interval_us, rmt_move_plan_t *plan) {
    if (!plan) return ESP_ERR_INVALID_ARG;
    memset(plan, 0, sizeof(*plan));

    int32_t abs_delta[AXIS_COUNT] = {0};
    int32_t err[AXIS_COUNT] = {0};
    uint32_t write_idx[AXIS_COUNT] = {0};

    for (int axis = 0; axis < AXIS_COUNT; ++axis) {
        abs_delta[axis] = abs(delta[axis]);
        if (abs_delta[axis] == 0) continue;
        if (!g_rmt_tx_chan[axis] || !g_rmt_copy_encoder[axis]) {
            ESP_LOGE(TAG, "Axis %d requested to move but has no STEP pin/RMT channel", axis);
            rmt_move_plan_free(plan);
            return ESP_ERR_NOT_SUPPORTED;
        }

        plan->symbols[axis] = calloc((size_t)abs_delta[axis], sizeof(rmt_symbol_word_t));
        if (!plan->symbols[axis]) {
            ESP_LOGE(TAG, "Failed to allocate RMT symbols for axis %d (%d steps)", axis, abs_delta[axis]);
            rmt_move_plan_free(plan);
            return ESP_ERR_NO_MEM;
        }
        plan->step_count[axis] = (uint32_t)abs_delta[axis];
        plan->active_mask |= (1U << axis);
    }

    for (int32_t master = 0; master < max_steps; ++master) {
        uint32_t interval_us = move_interval_with_ramp(base_interval_us, master, max_steps);
        for (int axis = 0; axis < AXIS_COUNT; ++axis) {
            if (abs_delta[axis] == 0) continue;
            err[axis] += abs_delta[axis];
            if (err[axis] >= max_steps) {
                plan->symbols[axis][write_idx[axis]++] = (rmt_symbol_word_t) {
                    .level0 = 1,
                    .duration0 = STEP_PULSE_US,
                    .level1 = 0,
                    .duration1 = interval_us - STEP_PULSE_US,
                };
                err[axis] -= max_steps;
            }
        }
    }

    for (int axis = 0; axis < AXIS_COUNT; ++axis) {
        if ((uint32_t)abs_delta[axis] != write_idx[axis]) {
            ESP_LOGE(TAG, "RMT move plan mismatch on axis %d (%" PRIu32 " != %d)", axis, write_idx[axis], abs_delta[axis]);
            rmt_move_plan_free(plan);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t rmt_execute_move_plan(const rmt_move_plan_t *plan) {
    if (!plan) return ESP_ERR_INVALID_ARG;
    if (plan->active_mask == 0) return ESP_OK;

    rmt_sync_manager_handle_t sync_mgr = rmt_get_sync_manager_for_mask(plan->active_mask);
    esp_err_t ret = ESP_OK;
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };

    esp_rom_delay_us(DIR_SETUP_DELAY_US);

    for (int axis = 0; axis < AXIS_COUNT; ++axis) {
        if (!(plan->active_mask & (1U << axis))) continue;
        size_t payload_size = (size_t)plan->step_count[axis] * sizeof(rmt_symbol_word_t);
        ret = rmt_transmit(g_rmt_tx_chan[axis], g_rmt_copy_encoder[axis], plan->symbols[axis], payload_size, &tx_cfg);
        if (ret != ESP_OK) goto out;
    }

    for (int axis = 0; axis < AXIS_COUNT; ++axis) {
        if (!(plan->active_mask & (1U << axis))) continue;
        ret = rmt_tx_wait_all_done(g_rmt_tx_chan[axis], -1);
        if (ret != ESP_OK) goto out;
    }

out:
    if (sync_mgr) {
        rmt_sync_reset(sync_mgr);
        rmt_del_sync_manager(sync_mgr);
    }
    return ret;
}

// ---------- Motion ----------
static void set_axis_direction(axis_id_t axis, bool positive) {
    const axis_hw_t *a = &g_axes[axis];
    if (!pin_is_valid(a->dir_pin)) return;
    bool level = positive;
    if (a->invert_dir) level = !level;
    gpio_set_level(a->dir_pin, level ? 1 : 0);
}

static esp_err_t move_linear_steps(const int32_t target_steps[AXIS_COUNT], float feed_mm_min) {
    xSemaphoreTakeRecursive(g_motion_mutex, portMAX_DELAY);

    esp_err_t ret = ESP_OK;
    int32_t start[AXIS_COUNT];
    int32_t delta[AXIS_COUNT];
    int32_t abs_delta[AXIS_COUNT];
    int32_t max_steps = 0;
    bool axis_active[AXIS_COUNT] = {false};
    rmt_move_plan_t plan;
    bool plan_built = false;

    if (machine_faulted()) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (feed_mm_min < 1.0f) feed_mm_min = DEFAULT_FEED_MM_MIN;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    memcpy(start, g_state.pos_steps, sizeof(start));
    xSemaphoreGive(g_state_mutex);

    for (int i = 0; i < AXIS_COUNT; ++i) {
        delta[i] = target_steps[i] - start[i];
        abs_delta[i] = abs(delta[i]);
        axis_active[i] = (abs_delta[i] > 0);

        if (axis_active[i] && !g_rmt_tx_chan[i]) {
            ESP_LOGE(TAG, "Axis %d requested to move but has no STEP pin/RMT channel", i);
            ret = ESP_ERR_NOT_SUPPORTED;
            goto cleanup;
        }

        if (abs_delta[i] > max_steps) max_steps = abs_delta[i];
        set_axis_direction((axis_id_t)i, delta[i] >= 0);
    }

    if (max_steps == 0) {
        ret = ESP_OK;
        goto cleanup;
    }

    float dx = steps_to_mm(AXIS_X, delta[AXIS_X]);
    float dy = steps_to_mm(AXIS_Y, delta[AXIS_Y]);
    float dz = steps_to_mm(AXIS_Z, delta[AXIS_Z]);
    float de = steps_to_mm(AXIS_E, delta[AXIS_E]);
    float path_mm = sqrtf(dx * dx + dy * dy + dz * dz + de * de);
    if (path_mm < 0.001f) {
        path_mm = fmaxf(fabsf(dx) + fabsf(dy) + fabsf(dz) + fabsf(de), 0.001f);
    }

    uint32_t interval_us = clamp_period_us(
        (uint32_t)fmaxf((path_mm / (feed_mm_min / 60.0f) * 1e6f) / (float)max_steps,
                        (float)(STEP_PULSE_US + 1))
    );

    ret = rmt_build_move_plan(delta, max_steps, interval_us, &plan);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    plan_built = true;

    for (int i = 0; i < AXIS_COUNT; ++i) {
        if (axis_active[i]) {
            set_axis_enable((axis_id_t)i, true);
        }
    }

    if (axis_active[AXIS_X] && is_switch_triggered(g_axes[AXIS_X].min_pin) && delta[AXIS_X] < 0) {
        latch_fault("Hit X min during move");
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (axis_active[AXIS_X] && is_switch_triggered(g_axes[AXIS_X].max_pin) && delta[AXIS_X] > 0) {
        latch_fault("Hit X max during move");
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (axis_active[AXIS_Y] && is_switch_triggered(g_axes[AXIS_Y].min_pin) && delta[AXIS_Y] < 0) {
        latch_fault("Hit Y min during move");
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (axis_active[AXIS_Y] && is_switch_triggered(g_axes[AXIS_Y].max_pin) && delta[AXIS_Y] > 0) {
        latch_fault("Hit Y max during move");
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (axis_active[AXIS_Z] && is_switch_triggered(g_axes[AXIS_Z].min_pin) && delta[AXIS_Z] < 0) {
        latch_fault("Hit Z min during move");
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (axis_active[AXIS_Z] && is_switch_triggered(g_axes[AXIS_Z].max_pin) && delta[AXIS_Z] > 0) {
        latch_fault("Hit Z max during move");
        ret = ESP_FAIL;
        goto cleanup;
    }

    ret = rmt_execute_move_plan(&plan);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    memcpy(g_state.pos_steps, target_steps, sizeof(g_state.pos_steps));
    xSemaphoreGive(g_state_mutex);

cleanup:
    if (plan_built) {
        rmt_move_plan_free(&plan);
    }
    if (ret != ESP_OK) {
        rmt_abort_all();
    }
    for (int i = 0; i < AXIS_COUNT; ++i) {
        if (axis_active[i]) {
            set_axis_enable((axis_id_t)i, false);
        }
    }
    xSemaphoreGiveRecursive(g_motion_mutex);
    return ret;
}

static esp_err_t home_single_axis(axis_id_t axis) {
    if (axis == AXIS_E) return ESP_OK;

    const axis_hw_t *a = &g_axes[axis];
    if (a->min_pin == GPIO_NUM_NC) return ESP_ERR_NOT_SUPPORTED;
    if (!g_rmt_tx_chan[axis]) return ESP_ERR_NOT_SUPPORTED;

    set_axis_enable(axis, true);

    if (is_switch_triggered(a->min_pin)) {
        ESP_RETURN_ON_ERROR(rmt_move_axis_steps(axis, true, (uint32_t)mm_to_steps(axis, HOMING_BACKOFF_MM), 1200), TAG, "home backoff failed");
    }

    uint32_t search_steps = (uint32_t)mm_to_steps(axis, 400.0f);
    while (!is_switch_triggered(a->min_pin) && search_steps > 0) {
        uint32_t chunk = search_steps > RMT_HOME_CHUNK_STEPS ? RMT_HOME_CHUNK_STEPS : search_steps;
        ESP_RETURN_ON_ERROR(rmt_move_axis_steps(axis, false, chunk, 900), TAG, "home seek failed");
        search_steps -= chunk;
    }
    if (!is_switch_triggered(a->min_pin)) {
        latch_fault("Homing switch not found");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(rmt_move_axis_steps(axis, true, (uint32_t)mm_to_steps(axis, HOMING_BACKOFF_MM), 1200), TAG, "home re-backoff failed");

    uint32_t slow_steps = (uint32_t)mm_to_steps(axis, HOMING_BACKOFF_MM * 2.0f);
    while (!is_switch_triggered(a->min_pin) && slow_steps > 0) {
        uint32_t chunk = slow_steps > RMT_HOME_CHUNK_STEPS ? RMT_HOME_CHUNK_STEPS : slow_steps;
        ESP_RETURN_ON_ERROR(rmt_move_axis_steps(axis, false, chunk, 2000), TAG, "home slow seek failed");
        slow_steps -= chunk;
    }
    if (!is_switch_triggered(a->min_pin)) {
        latch_fault("Slow homing switch not found");
        return ESP_FAIL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_state.pos_steps[axis] = 0;
    g_state.homed[axis] = true;
    xSemaphoreGive(g_state_mutex);

    set_axis_enable(axis, false);
    ESP_LOGI(TAG, "Axis %c homed", axis == AXIS_X ? 'X' : axis == AXIS_Y ? 'Y' : 'Z');
    return ESP_OK;
}

static esp_err_t home_all_axes(void) {
    xSemaphoreTakeRecursive(g_motion_mutex, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if ((ret = home_single_axis(AXIS_Z)) != ESP_OK) goto done;
    if ((ret = home_single_axis(AXIS_X)) != ESP_OK) goto done;
    if ((ret = home_single_axis(AXIS_Y)) != ESP_OK) goto done;
done:
    if (ret != ESP_OK) {
        rmt_abort_all();
    }
    set_all_axes_enabled(false);
    xSemaphoreGiveRecursive(g_motion_mutex);
    return ret;
}

static esp_err_t jog_relative(float x, float y, float z, float e, float f) {
    int32_t target[AXIS_COUNT];
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    memcpy(target, g_state.pos_steps, sizeof(target));
    xSemaphoreGive(g_state_mutex);

    target[AXIS_X] += mm_to_steps(AXIS_X, x);
    target[AXIS_Y] += mm_to_steps(AXIS_Y, y);
    target[AXIS_Z] += mm_to_steps(AXIS_Z, z);
    target[AXIS_E] += mm_to_steps(AXIS_E, e);
    return move_linear_steps(target, f > 0 ? f : JOG_FEED_MM_MIN);
}

// ---------- Console ----------
static void print_status(void) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    printf("X=%.3f Y=%.3f Z=%.3f E=%.3f | Bed=%.1f/%.1f | abs=%d | printing=%d | fault=%d\n",
           steps_to_mm(AXIS_X, g_state.pos_steps[AXIS_X]),
           steps_to_mm(AXIS_Y, g_state.pos_steps[AXIS_Y]),
           steps_to_mm(AXIS_Z, g_state.pos_steps[AXIS_Z]),
           steps_to_mm(AXIS_E, g_state.pos_steps[AXIS_E]),
           g_state.bed_temp_c,
           g_state.bed_target_c,
           g_state.absolute_mode,
           g_state.printing,
           g_state.faulted);
    if (g_state.faulted) {
        printf("FAULT: %s\n", g_state.fault_msg);
    }
    xSemaphoreGive(g_state_mutex);
}

static void print_help(void) {
    printf("\nCommands:\n");
    printf("  status\n");
    printf("  home\n");
    printf("  jog X10 Y0 Z0 E0.5 F1200\n");
    printf("  temp 60        or  M140 S60\n");
    printf("  run /sdcard/job.gcode\n");
    printf("  stop\n");
    printf("  Any supported G-code: G0/G1/G28/G90/G91/G92/M105/M140/M190/M112/M18/M84\n\n");
}

static void move_task(void *arg)
{
    while (1)
    {
        if (xSemaphoreTake(move_semaphore, portMAX_DELAY) == pdTRUE)
        {
            int32_t target[AXIS_COUNT];
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            memcpy(target, g_state.pos_steps, sizeof(target));
            xSemaphoreGive(g_state_mutex);

            if (!isnan(ui_cmd.x)) target[AXIS_X] = mm_to_steps(AXIS_X, ui_cmd.x);
            if (!isnan(ui_cmd.y)) target[AXIS_Y] = mm_to_steps(AXIS_Y, ui_cmd.y);
            if (!isnan(ui_cmd.z)) target[AXIS_Z] = mm_to_steps(AXIS_Z, ui_cmd.z);
            if (!isnan(ui_cmd.e)) target[AXIS_E] = mm_to_steps(AXIS_E, ui_cmd.e);

            move_linear_steps(target, ui_cmd.f > 0 ? ui_cmd.f : DEFAULT_FEED_MM_MIN);
        }
    }
}

static void stop_task(void *arg)
{
    while (1)
    {
        if (xSemaphoreTake(stop_semaphore, portMAX_DELAY) == pdTRUE)
        {
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_state.stop_requested = true;
            g_state.printing = false;
            g_state.heater_enabled = false;
            g_state.bed_target_c = 0.0f;
            xSemaphoreGive(g_state_mutex);
            if (PIN_BED_SSR != GPIO_NUM_NC) gpio_set_level(PIN_BED_SSR, 0);
            set_all_axes_enabled(false);
        }
    }
}

static void console_task(void *arg) {
    (void)arg;
    char* line;

    print_help();
    while (1) {
        // printf("> ");
        fflush(stdout);
        // Read line with prompt
        line = linenoise("esp> ");
        if (line == NULL) {
            // Free buffer
            linenoiseFree(line);
            continue; // Empty line
        }
        // Process line
        printf("Received: %s\n", line);

        line[strcspn(line, "\r\n")] = '\0';
        char *cmd = skip_ws(line);
        printf("Received: %s\n", line);

        if (*cmd == '\0') {
            // Free buffer
            linenoiseFree(line);
            continue;
        }

        if (strcasecmp(cmd, "help") == 0) {
            print_help();
            // Free buffer
            linenoiseFree(line);
            continue;
        }
        if (strcasecmp(cmd, "status") == 0) {
            print_status();
            // Free buffer
            linenoiseFree(line);
            continue;
        }
        if (strcasecmp(cmd, "stop") == 0) {
            xSemaphoreGive(stop_semaphore);
            ESP_LOGW(TAG, "Stop requested");
            // Free buffer
            linenoiseFree(line);
            continue;
        }
        if (strncasecmp(cmd, "run ", 4) == 0) {
            char *path = skip_ws(cmd + 4);
            run_gcode_file(path);
            // Free buffer
            linenoiseFree(line);
            continue;
        }

        execute_gcode_line(cmd);

        // Add to history
        linenoiseHistoryAdd(line);
        // Free buffer
        linenoiseFree(line);
    }
}

// ---------- Init ----------
static void gpio_init_all(void) {
    uint64_t out_mask = 0;
    uint64_t in_mask = 0;

    for (int i = 0; i < AXIS_COUNT; ++i) {
        out_mask = add_pin_to_mask(out_mask, g_axes[i].step_pin);
        out_mask = add_pin_to_mask(out_mask, g_axes[i].dir_pin);
        out_mask = add_pin_to_mask(out_mask, g_axes[i].en_pin);
        in_mask = add_pin_to_mask(in_mask, g_axes[i].min_pin);
        in_mask = add_pin_to_mask(in_mask, g_axes[i].max_pin);
    }
    out_mask = add_pin_to_mask(out_mask, PIN_BED_SSR);
    in_mask = add_pin_to_mask(in_mask, PIN_Z_PROBE);

    if (out_mask) {
        gpio_config_t out_cfg = {
            .pin_bit_mask = out_mask,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&out_cfg));
    }

    if (in_mask) {
        gpio_config_t in_cfg = {
            .pin_bit_mask = in_mask,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&in_cfg));
    }

    for (int i = 0; i < AXIS_COUNT; ++i) {
        set_axis_enable((axis_id_t)i, false);
        if (pin_is_valid(g_axes[i].step_pin)) gpio_set_level(g_axes[i].step_pin, 0);
        if (pin_is_valid(g_axes[i].dir_pin)) gpio_set_level(g_axes[i].dir_pin, 0);
    }
    if (pin_is_valid(PIN_BED_SSR)) gpio_set_level(PIN_BED_SSR, 0);
}

void app_main(void) {

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_new_repl_uart(&uart_config, &repl_config, &repl);

    stop_semaphore = xSemaphoreCreateBinary();
    move_semaphore = xSemaphoreCreateBinary();
    g_state_mutex = xSemaphoreCreateMutex();
    g_motion_mutex = xSemaphoreCreateRecursiveMutex();
    g_ui_cmd_queue = xQueueCreate(UI_CMD_QUEUE_LEN, sizeof(ui_cmd_t));
    memset(&g_state, 0, sizeof(g_state));
    g_state.absolute_mode = true;

    gpio_init_all();
    ESP_ERROR_CHECK(rmt_motion_init());
    ESP_ERROR_CHECK(thermistor_adc_init());

    if (sdcard_init() != ESP_OK) {
        ESP_LOGW(TAG, "Continuing without SD card. You can still jog/home over serial.");
    }

    // if (ui_display_touch_init() != ESP_OK) {
    //     ESP_LOGW(TAG, "Continuing without touchscreen UI.");
    // } else {
    //     xTaskCreatePinnedToCore(ui_motion_task, "ui_motion_task", 8192, NULL, 4, NULL, 1);
    // }

    // xTaskCreatePinnedToCore(heater_task, "heater_task", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(console_task, "console_task", 8192, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(stop_task, "stop_task", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(move_task, "move_task", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(temperature_task, "temperature_task", 4096, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "Ink printer milestone firmware ready");
}
