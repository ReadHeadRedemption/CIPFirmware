#include "stepperMotor.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include <math.h>

static const char *TAG = "STEPPER_RMT";

// RMT Specific handles
static rmt_channel_handle_t motor_channels[NUM_MOTORS] = {NULL};
static rmt_encoder_handle_t motor_encoders[NUM_MOTORS] = {NULL};

// Motor configurations
static motor_config_t motors[NUM_MOTORS] = {
    {xStep, xDir, 0, NULL, 0, 0, tempEnable},
    {yStep, yDir, 0, NULL, 0, 0, tempEnable},
    {zStep, zDir, 0, NULL, 0, 0, tempEnable},
    {eStep, eDir, 0, NULL, 0, 0, tempEnable},
};

// Simple pulse structure for RMT
typedef struct
{
    uint32_t freq_hz;
    uint32_t step_count;
} stepper_payload_t;

/**
 * @brief RMT Encoder: Converts frequency to RMT symbols (High/Low durations)
 */
size_t rmt_encode_stepper(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_symbol_word_t *symbols)
{
    stepper_payload_t *payload = (stepper_payload_t *)primary_data;
    if (payload->freq_hz == 0)
        return 0;

    // Use the resolution you set in init (1MHz)
    // Ticks = Resolution / (2 * Frequency)
    uint32_t resolution = 1000000;
    uint32_t ticks_per_half = resolution / (2 * payload->freq_hz);

    for (int i = 0; i < payload->step_count; i++)
    {
        symbols[i] = (rmt_symbol_word_t){
            .duration0 = (uint16_t)ticks_per_half,
            .level0 = 1,
            .duration1 = (uint16_t)ticks_per_half,
            .level1 = 0};
    }
    return payload->step_count;
}

// -------------------------------------------------------------------------
// MOTOR INITIALIZATION
// -------------------------------------------------------------------------

esp_err_t stepper_motor_init(void)
{
    ESP_LOGI(TAG, "Initializing RMT Stepper Channels...");
    uint32_t step_pins[] = {xStep, yStep, zStep, eStep};
    uint32_t dir_pins[] = {xDir, yDir, zDir, eDir};

    // 1. Create a generic copy encoder configuration
    rmt_copy_encoder_config_t copy_encoder_config = {};

    for (int i = 0; i < NUM_MOTORS; i++)
    {
        // 2. Configure TX Channel
        rmt_tx_channel_config_t tx_chan_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .gpio_num = step_pins[i],
            .mem_block_symbols = 64,
            .resolution_hz = 1000000,
            .trans_queue_depth = 10,
        };
        ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &motor_channels[i]));

        // 3. Create the copy encoder for this channel <-- FIX HERE
        ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &motor_encoders[i]));

        // 4. Setup Direction Pins
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << dir_pins[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);

        // 5. Enable RMT Channel
        ESP_ERROR_CHECK(rmt_enable(motor_channels[i]));

        motors[i].dir_pin = dir_pins[i];
        motors[i].frequency_hz = 0;
        motors[i].targetStep = 0;
    }

    ESP_LOGI(TAG, "RMT Stepper initialized");
    return ESP_OK;
}

// -------------------------------------------------------------------------
// MOTOR CONTROL
// -------------------------------------------------------------------------

esp_err_t stepper_set_direction(motor_id_t motor_id, uint8_t direction)
{
    if (motor_id >= NUM_MOTORS)
        return ESP_ERR_INVALID_ARG;
    return gpio_set_level(motors[motor_id].dir_pin, direction);
}

// Note: In RMT, we usually transmit chunks.
// For real-time frequency changes, we send small batches of steps.
static esp_err_t rmt_step_burst(motor_id_t motor_id, uint32_t freq, uint32_t count)
{
    if (freq == 0 || count == 0)
        return ESP_OK;

    uint32_t ticks = 1000000 / (2 * freq);
    rmt_symbol_word_t symbol = {{{ticks, 1, ticks, 0}}};

    rmt_transmit_config_t tx_config = {
        .loop_count = count - 1, // RMT loops the symbol N times
    };

    return rmt_transmit(motor_channels[motor_id], motor_encoders[motor_id], &symbol, 1, &tx_config);
}

// -------------------------------------------------------------------------
// COORDINATED MOVEMENT
// -------------------------------------------------------------------------
esp_err_t coordinated_move(MoveCmd_t *move)
{
    float StepPerMM[] = {xStepsPerMM, yStepsPerMM, zStepsPerMM, eStepsPerMM};

    // 1. Calculate steps
    float dx = move->target_x - motors[MOTOR_X].position;
    float dy = move->target_y - motors[MOTOR_Y].position;
    float dz = move->target_z - motors[MOTOR_Z].position;

    uint32_t steps[3] = {
        (uint32_t)(fabs(dx) * StepPerMM[0]),
        (uint32_t)(fabs(dy) * StepPerMM[1]),
        (uint32_t)(fabs(dz) * StepPerMM[2])};

    uint32_t max_steps = 0;
    for (int i = 0; i < 3; i++)
        if (steps[i] > max_steps)
            max_steps = steps[i];

    if (max_steps == 0)
        return ESP_OK;

    // 2. Set Directions
    stepper_set_direction(MOTOR_X, (dx >= 0) ? 0 : 1);
    stepper_set_direction(MOTOR_Y, (dy >= 0) ? 0 : 1);
    stepper_set_direction(MOTOR_Z, (dz >= 0) ? 0 : 1);

    // 3. Movement Loop Setup
    uint32_t steps_taken = 0;
    const uint32_t chunk_size = 10;
    // Pre-allocate a symbol buffer matching the maximum possible batch chunk size
    rmt_symbol_word_t sym_buffer[chunk_size];

    // 4. Movement Loop
    while (steps_taken < max_steps)
    {
        uint32_t batch = (max_steps - steps_taken < chunk_size) ? (max_steps - steps_taken) : chunk_size;

        for (int i = 0; i < 3; i++)
        {
            if (steps[i] > 0)
            {
                // Scale frequency for sub-axes based directly on the target feed rate
                float ratio = (float)steps[i] / max_steps;
                uint32_t axis_freq = (uint32_t)(move->feed_rate_hz * ratio);
                uint32_t axis_steps = (uint32_t)(batch * ratio);

                // Prevent math issues if axis_steps scales down to 0 for this specific chunk
                if (axis_steps > 0 && axis_freq > 0)
                {
                    uint32_t ticks = 1000000 / (2 * axis_freq);

                    // Fill our array buffer with the identical pulses needed
                    for (uint32_t s = 0; s < axis_steps; s++)
                    {
                        sym_buffer[s] = (rmt_symbol_word_t){
                            .duration0 = (uint16_t)ticks,
                            .level0 = 1,
                            .duration1 = (uint16_t)ticks,
                            .level1 = 0};
                    }

                    // Loop count must stay 0 because hardware loop count isn't supported on this SoC
                    rmt_transmit_config_t tx_conf = {
                        .loop_count = 0};

                    // Pass NULL as the encoder handle to use the built-in copy encoder,
                    // which directly copies raw symbols from sym_buffer to the RMT hardware memory.
                    ESP_ERROR_CHECK(rmt_transmit(
                        motor_channels[i],
                        motor_encoders[i], // Use the encoder handle you created
                        sym_buffer,
                        axis_steps * sizeof(rmt_symbol_word_t), // Must be size in BYTES, not count
                        &tx_conf));
                        
                    rmt_tx_wait_all_done(motor_channels[i], -1);
                }
            }
        }

        steps_taken += batch;

        // Keep a close eye on this. Because rmt_transmit is non-blocking by default unless specified,
        // you must ensure the RMT hardware has finished transmitting the buffer before mutating it.
        // If it stutters, you may need to add rmt_tx_wait_all_done() here.
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    // 5. Update positions
    motors[MOTOR_X].position = move->target_x;
    motors[MOTOR_Y].position = move->target_y;
    motors[MOTOR_Z].position = move->target_z;

    return ESP_OK;
}