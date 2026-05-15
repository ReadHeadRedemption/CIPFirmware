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
    {xStep, xDir, 0, NULL, 0, 0.0, tempEnable},
    {yStep, yDir, 0, NULL, 0, 0.0, tempEnable},
    {zStep, zDir, 0, NULL, 0, 0.0, tempEnable},
    {eStep, eDir, 0, NULL, 0, 0.0, tempEnable},
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
    //ESP_LOGI(TAG, "SET MOTOR %d: %d", motor_id, direction);
    return gpio_set_level(motors[motor_id].dir_pin, direction);
}

esp_err_t stepper_set_frequency(motor_id_t motor_id, uint32_t frequency)
{
    if (motor_id >= NUM_MOTORS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    motors[motor_id].frequency_hz = frequency;

    if (frequency == 0)
    {
        // Stop the current RMT transmission without disabling the channel
        rmt_tx_wait_all_done(motor_channels[motor_id], -1);
        vTaskDelay(pdMS_TO_TICKS(2));
        return ESP_OK;
    }
    // Channel stays enabled from init — just transmit
    uint32_t ticks = 1000000 / (2 * frequency);

    rmt_symbol_word_t symbol = {
        .duration0 = (uint16_t)ticks,
        .level0 = 1,
        .duration1 = (uint16_t)ticks,
        .level1 = 0};

    rmt_transmit_config_t tx_config = {
        .loop_count = -1,
    };

    return rmt_transmit(motor_channels[motor_id],
                        motor_encoders[motor_id],
                        &symbol,
                        sizeof(rmt_symbol_word_t),
                        &tx_config);
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

// move all motors to a organized point
esp_err_t coordinated_move(MoveCmd_t *move)
{
    float StepPerMM[] = {xStepsPerMM, yStepsPerMM, zStepsPerMM, eStepsPerMM};

    // 1. Calculate target steps
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
    
    // --- ACCELERATION CONFIGURATION ---
    uint32_t min_feed_rate = 200; // Minimum frequency (Hz) to start/end movement safely
    if (min_feed_rate > move->feed_rate_hz) {
        min_feed_rate = move->feed_rate_hz;
    }

    // Number of steps over which to accelerate/decelerate.
    // Capped at max_steps / 2 to ensure a clean profile on ultra-short moves.
    uint32_t accel_steps = 300; // Adjust this value to change acceleration aggressiveness
    if (accel_steps > max_steps / 2) {
        accel_steps = max_steps / 2;
    }
    // ----------------------------------
    
    // Independent buffers per axis to prevent memory overwrites
    rmt_symbol_word_t sym_buffer[3][chunk_size]; 

    // BRESENHAM ACCUMULATORS: Tracks fractional steps left over from truncation
    float step_error[3] = {0.0f, 0.0f, 0.0f};

    // 4. Movement Loop
    while (steps_taken < max_steps)
    {
        uint32_t batch = (max_steps - steps_taken < chunk_size) ? (max_steps - steps_taken) : chunk_size;
        bool channel_active[3] = {false, false, false};

        // --- DYNAMIC FEED RATE CALCULATION ---
        uint32_t current_feed_rate = move->feed_rate_hz;

        if (steps_taken < accel_steps)
        {
            // RAMP UP: Scale speed linearly from min_feed_rate to target feed_rate
            current_feed_rate = min_feed_rate + ((move->feed_rate_hz - min_feed_rate) * steps_taken) / accel_steps;
        }
        else if (steps_taken > (max_steps - accel_steps))
        {
            // RAMP DOWN: Scale speed linearly back down to min_feed_rate
            uint32_t steps_from_end = max_steps - steps_taken;
            current_feed_rate = min_feed_rate + ((move->feed_rate_hz - min_feed_rate) * steps_from_end) / accel_steps;
        }
        // --------------------------------------

        // Step A: Queue up and start all active motors simultaneously
        for (int i = 0; i < 3; i++)
        {
            if (steps[i] > 0)
            {
                // Calculate exactly how many ideal steps this axis should take in this batch
                float ratio = (float)steps[i] / max_steps;
                float ideal_steps = batch * ratio;

                // Add the accumulated error from previous chunks (Bresenham step)
                float total_steps_owed = ideal_steps + step_error[i];

                // Extract the integer number of steps to actually execute right now
                uint32_t axis_steps = (uint32_t)total_steps_owed;

                // Keep the fractional remainder for the next loop iteration
                step_error[i] = total_steps_owed - (float)axis_steps;

                // Only transmit if the accumulator triggered an actual integer step
                if (axis_steps > 0)
                {
                    // Recalculate frequency using our new dynamic current_feed_rate
                    uint32_t axis_freq = (uint32_t)((axis_steps * current_feed_rate) / batch);

                    if (axis_freq > 0)
                    {
                        uint32_t ticks = 1000000 / (2 * axis_freq);
                        
                        // Prevent low-speed clock overflow (RMT duration limit is 15-bit)
                        if (ticks > 32767) ticks = 32767; 
                        // Prevent high-speed narrow pulse issues (DRV8825 limit)
                        if (ticks < 3) ticks = 3; 

                        // Populate the buffer for this specific axis
                        for (uint32_t s = 0; s < axis_steps; s++)
                        {
                            sym_buffer[i][s] = (rmt_symbol_word_t){
                                .duration0 = (uint16_t)ticks,
                                .level0 = 1,
                                .duration1 = (uint16_t)ticks,
                                .level1 = 0};
                        }

                        rmt_transmit_config_t tx_conf = { .loop_count = 0 };

                        ESP_ERROR_CHECK(rmt_transmit(
                            motor_channels[i],
                            motor_encoders[i], 
                            sym_buffer[i], 
                            axis_steps * sizeof(rmt_symbol_word_t), 
                            &tx_conf));
                        
                        channel_active[i] = true;
                    }
                }
            }
        }

        // Step B: Wait for all active channels to complete their chunk bursts
        for (int i = 0; i < 3; i++)
        {
            if (channel_active[i])
            {
                rmt_tx_wait_all_done(motor_channels[i], -1);
            }
        }

        steps_taken += batch;
    }

    // 5. Update positions
    motors[MOTOR_X].position = move->target_x;
    motors[MOTOR_Y].position = move->target_y;
    motors[MOTOR_Z].position = move->target_z;

    return ESP_OK;
}

esp_err_t homeMotors()
{
    // Track which axes have been homed
    bool axis_homed[3] = {false, false, false};
    
    // Set initial positions (arbitrary non-zero values)
    motors[MOTOR_X].position = 999.0f;
    motors[MOTOR_Y].position = 999.0f;
    motors[MOTOR_Z].position = 999.0f;

    // 1. CRITICAL: Initialize the homer struct to match the current fake positions
    MoveCmd_t homer = {0}; 
    homer.target_x = motors[MOTOR_X].position;
    homer.target_y = motors[MOTOR_Y].position;
    homer.target_z = motors[MOTOR_Z].position;

    // Move off of limit switch if on
    ESP_LOGI(TAG, "Starting coordinated homing...");
    homer.target_x += 7.0f;
    homer.target_y += 7.0f;
    homer.target_z += 7.0f;
    homer.feed_rate_hz = 5000;
    coordinated_move(&homer);

    // Prepare for homing moves
    homer.feed_rate_hz = 10000;

    // ----------------------------------------------------------------
    // HOME X AXIS
    // ----------------------------------------------------------------
    // Flush any stale switch triggers before starting the loop
    xSemaphoreTake(xSwitchSemaphore, 0); 
    
    while (!axis_homed[MOTOR_X])
    {
        homer.target_x -= 1.0f;
        
        // 2. CRITICAL: Actually command the motor to move!
        coordinated_move(&homer); 

        // Check if the switch triggered during that 1mm move
        if (xSemaphoreTake(xSwitchSemaphore, 0) == pdTRUE) 
        {
            motors[MOTOR_X].position = 0.0;
            homer.target_x = 0.0; // Reset target so Y/Z moves don't go crazy
            axis_homed[MOTOR_X] = true;
            ESP_LOGI(TAG, "X axis homed");
        }
    }

    // ----------------------------------------------------------------
    // HOME Y AXIS
    // ----------------------------------------------------------------
    xSemaphoreTake(ySwitchSemaphore, 0);
    while (!axis_homed[MOTOR_Y])
    {
        homer.target_y -= 1.0f;
        
        coordinated_move(&homer); // Command the move
        
        if (xSemaphoreTake(ySwitchSemaphore, 0) == pdTRUE)
        {
            motors[MOTOR_Y].position = 0.0;
            homer.target_y = 0.0;
            axis_homed[MOTOR_Y] = true;
            ESP_LOGI(TAG, "Y axis homed");
        }
    }

    // ----------------------------------------------------------------
    // HOME Z AXIS
    // ----------------------------------------------------------------
    xSemaphoreTake(zSwitchSemaphore, 0);
    while (!axis_homed[MOTOR_Z])
    {
        homer.target_z -= 1.0f;
        
        coordinated_move(&homer); // Command the move
        
        if (xSemaphoreTake(zSwitchSemaphore, 0) == pdTRUE)
        {
            motors[MOTOR_Z].position = 0.0;
            homer.target_z = 0.0;
            axis_homed[MOTOR_Z] = true;
            ESP_LOGI(TAG, "Z axis homed");
        }
    }

    // Move to offset position
    homer.target_x = 32.5f;
    homer.target_y = 230.0f;
    homer.target_z = 0.0f; // Keep Z at 0
    coordinated_move(&homer);
    
    // Set actual absolute zero
    motors[MOTOR_X].position = 0;
    motors[MOTOR_Y].position = 0;

    ESP_LOGI(TAG, "Homed");
    return ESP_OK;
}