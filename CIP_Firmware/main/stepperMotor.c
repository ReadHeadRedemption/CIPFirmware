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
    {xStep, xDir, 0, NULL, 0, 0.0, xEnable},
    {yStep, yDir, 0, NULL, 0, 0.0, yEnable},
    {zStep, zDir, 0, NULL, 0, 0.0, zEnable},
    {eStep, eDir, 0, NULL, 0, 0.0, eEnable},
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
            .mem_block_symbols = 48,
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
    esp_err_t err;
    if (motor_id >= NUM_MOTORS)
        err = ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        // ESP_LOGI(TAG, "SET MOTOR %d: %d", motor_id, direction);
        err = gpio_set_level(motors[motor_id].dir_pin, direction);
        xSemaphoreGive(i2c_mutex);
    }
    return err;
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
// move all motors to a organized point
// move all motors to a organized point

float K_FACTOR = 0.04f;

esp_err_t kfact(int tool)
{
    // Condutive Ink, Solder Paste Ink, Camera
    float toolKval[3] = {0.04f, 0.01f, 0.0f};
    K_FACTOR = toolKval[tool];
    printf("K_FACTOR set to %.3f for tool %d\n", K_FACTOR, tool);
    return ESP_OK;
}

esp_err_t coordinated_move(MoveCmd_t *move)
{

    float StepPerMM[] = {xStepsPerMM, yStepsPerMM, zStepsPerMM, eStepsPerMM};

    // printf("hi!");

    // 1. Calculate target steps
    float dx = move->target_x - motors[MOTOR_X].position;
    float dy = move->target_y - motors[MOTOR_Y].position;
    float dz = move->target_z - motors[MOTOR_Z].position;
    float de = move->moveE - motors[MOTOR_E].position;

    uint32_t steps[4] = {
        (uint32_t)(fabs(dx) * StepPerMM[0]),
        (uint32_t)(fabs(dy) * StepPerMM[1]),
        (uint32_t)(fabs(dz) * StepPerMM[2]),
        (uint32_t)(fabs(de) * StepPerMM[3])};

    uint32_t max_steps = 0;
    for (int i = 0; i < 4; i++)
        if (steps[i] > max_steps)
            max_steps = steps[i];

    if (max_steps == 0)
        return ESP_OK;

    // 2. Set Directions for X, Y, Z
    stepper_set_direction(MOTOR_X, (dx >= 0) ? 0 : 1);
    stepper_set_direction(MOTOR_Y, (dy >= 0) ? 0 : 1);
    stepper_set_direction(MOTOR_Z, (dz >= 0) ? 1 : 0);

    uint8_t e_forward_dir = (de >= 0) ? 1 : 0;
    uint8_t e_reverse_dir = (de >= 0) ? 0 : 1;

    esp_rom_delay_us(5);

    // 3. Movement Loop Setup
    uint32_t steps_taken = 0;
    static const uint32_t chunk_size = 10;

    float v_max = (float)move->feed_rate_hz;
    float v_min = (float)move->feed_rate_hz;
    if (v_max < v_min)
    {
        v_max = v_min;
    }

    float accel_rate = 10000.0f;
    uint32_t accel_steps = (uint32_t)((v_max * v_max - v_min * v_min) / (2.0f * accel_rate));

    if (accel_steps > max_steps / 2)
    {
        accel_steps = max_steps / 2;
    }

    uint32_t decel_start_step = max_steps - accel_steps;

// --- MEMORY FIX: Decoupled and expanded buffer size to handle E-axis bursts ---
#define MAX_SYM_BUFFER 256
    rmt_symbol_word_t sym_buffer[4][MAX_SYM_BUFFER];

    static float step_error[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int32_t actual_steps[4] = {0, 0, 0, 0};
    float current_ideal_e = 0.0f;

    // 4. Movement Loop
    while (steps_taken < max_steps)
    {
        uint32_t batch = (max_steps - steps_taken < chunk_size) ? (max_steps - steps_taken) : chunk_size;
        bool channel_active[4] = {false, false, false, false};

        uint32_t current_feed_rate = move->feed_rate_hz;

        if (steps_taken < accel_steps)
        {
            current_feed_rate = (uint32_t)sqrtf(v_min * v_min + 2.0f * accel_rate * steps_taken);
        }
        else if (steps_taken >= decel_start_step)
        {
            uint32_t steps_from_end = max_steps - steps_taken;
            current_feed_rate = (uint32_t)sqrtf(v_min * v_min + 2.0f * accel_rate * steps_from_end);
        }

        if (current_feed_rate < (uint32_t)v_min)
            current_feed_rate = (uint32_t)v_min;
        if (current_feed_rate > move->feed_rate_hz)
            current_feed_rate = move->feed_rate_hz;

        // Step A1: Queue X, Y, Z
        for (int i = 0; i < 3; i++)
        {
            if (steps[i] > 0)
            {
                float ratio = (float)steps[i] / max_steps;
                float ideal_steps = batch * ratio;
                float total_steps_owed = ideal_steps + step_error[i];
                uint32_t axis_steps = (uint32_t)total_steps_owed;

                // --- MEMORY FIX: Safety clamp for X, Y, Z ---
                if (axis_steps > MAX_SYM_BUFFER)
                    axis_steps = MAX_SYM_BUFFER;

                step_error[i] = total_steps_owed - (float)axis_steps;

                if (axis_steps > 0)
                {
                    actual_steps[i] += axis_steps;
                    uint32_t axis_freq = (uint32_t)((axis_steps * current_feed_rate) / batch);

                    if (axis_freq > 0)
                    {
                        uint32_t ticks = 1000000 / (2 * axis_freq);

                        if (ticks > 32767)
                            ticks = 32767;
                        if (ticks < 3)
                            ticks = 3;

                        for (uint32_t s = 0; s < axis_steps; s++)
                        {
                            sym_buffer[i][s] = (rmt_symbol_word_t){
                                .duration0 = (uint16_t)ticks, .level0 = 1, .duration1 = (uint16_t)ticks, .level1 = 0};
                        }

                        rmt_transmit_config_t tx_conf = {.loop_count = 0};
                        ESP_ERROR_CHECK(rmt_transmit(motor_channels[i], motor_encoders[i], sym_buffer[i], axis_steps * sizeof(rmt_symbol_word_t), &tx_conf));
                        channel_active[i] = true;
                    }
                }
            }
        }

        // Step A2: Queue E (Linear Advance Logic)
        if (steps[MOTOR_E] > 0)
        {
            float base_ratio = (float)(steps_taken + batch) / max_steps;
            float kinematic_e_steps = steps[MOTOR_E] * base_ratio;

            float advance_e_steps = 0.0f;
            if (steps_taken + batch < max_steps)
            {
                advance_e_steps = K_FACTOR * (float)current_feed_rate;
            }

            float target_e_steps = kinematic_e_steps + advance_e_steps;
            float delta_e = target_e_steps - current_ideal_e;
            current_ideal_e = target_e_steps;

            delta_e += step_error[MOTOR_E];
            int32_t e_steps_to_take = (int32_t)delta_e;
            uint32_t abs_e_steps = (uint32_t)abs(e_steps_to_take);

            // --- MEMORY FIX: Safety clamp for E-axis pressure bursts ---
            if (abs_e_steps > MAX_SYM_BUFFER)
            {
                abs_e_steps = MAX_SYM_BUFFER;
                e_steps_to_take = (e_steps_to_take >= 0) ? MAX_SYM_BUFFER : -MAX_SYM_BUFFER;
            }

            // Update step error AFTER clamping, so deferred steps are carried over safely
            step_error[MOTOR_E] = delta_e - (float)e_steps_to_take;

            if (abs_e_steps > 0)
            {
                stepper_set_direction(MOTOR_E, (e_steps_to_take >= 0) ? e_forward_dir : e_reverse_dir);
                esp_rom_delay_us(2);

                actual_steps[MOTOR_E] += e_steps_to_take;

                uint32_t axis_freq = (uint32_t)((abs_e_steps * current_feed_rate) / batch);

                if (axis_freq > 0)
                {
                    uint32_t ticks = 1000000 / (2 * axis_freq);

                    if (ticks > 32767)
                        ticks = 32767;
                    if (ticks < 3)
                        ticks = 3;

                    for (uint32_t s = 0; s < abs_e_steps; s++)
                    {
                        sym_buffer[MOTOR_E][s] = (rmt_symbol_word_t){
                            .duration0 = (uint16_t)ticks, .level0 = 1, .duration1 = (uint16_t)ticks, .level1 = 0};
                    }

                    rmt_transmit_config_t tx_conf = {.loop_count = 0};
                    ESP_ERROR_CHECK(rmt_transmit(motor_channels[MOTOR_E], motor_encoders[MOTOR_E], sym_buffer[MOTOR_E], abs_e_steps * sizeof(rmt_symbol_word_t), &tx_conf));
                    channel_active[MOTOR_E] = true;
                }
            }
        }

        // Step B: Wait for chunk to finish
        for (int i = 0; i < 4; i++)
        {
            if (channel_active[i])
            {
                rmt_tx_wait_all_done(motor_channels[i], -1);
            }
        }

        steps_taken += batch;
    }

    // 5. Update positions
    motors[MOTOR_X].position += (actual_steps[0] / StepPerMM[0]) * ((dx >= 0) ? 1.0f : -1.0f);
    motors[MOTOR_Y].position += (actual_steps[1] / StepPerMM[1]) * ((dy >= 0) ? 1.0f : -1.0f);
    motors[MOTOR_Z].position += (actual_steps[2] / StepPerMM[2]) * ((dz >= 0) ? 1.0f : -1.0f);
    motors[MOTOR_E].position += (actual_steps[3] / StepPerMM[3]) * ((de >= 0) ? 1.0f : -1.0f);

    for (int i = 0; i < 4; i++)
        motors[i].targetStep += actual_steps[i];

    return ESP_OK;
}

esp_err_t circular_move(MoveCmd_t *arc)
{
    float StepPerMM[] = {xStepsPerMM, yStepsPerMM, zStepsPerMM, eStepsPerMM};

    // 1. Calculate arc center in absolute coordinates
    float center_x = motors[MOTOR_X].position + arc->center_x;
    float center_y = motors[MOTOR_Y].position + arc->center_y;

    // 2. Calculate start and end angles
    float r_start_x = motors[MOTOR_X].position - center_x;
    float r_start_y = motors[MOTOR_Y].position - center_y;
    float r_end_x = arc->target_x - center_x;
    float r_end_y = arc->target_y - center_y;

    float radius = sqrtf(r_start_x * r_start_x + r_start_y * r_start_y);
    float start_angle = atan2f(r_start_y, r_start_x);
    float end_angle = atan2f(r_end_y, r_end_x);

    float end_radius = sqrtf(r_end_x * r_end_x + r_end_y * r_end_y);
    if (fabsf(radius - end_radius) > 0.5f)
    {
        return ESP_ERR_INVALID_ARG; // Arc doesn't close properly
    }

    // 3. Calculate angle difference
    float angle_diff = end_angle - start_angle;

    if (arc->circleDir) // G2: Clockwise
    {
        if (angle_diff > 0)
            angle_diff -= 2.0f * M_PI;
    }
    else // G3: Counterclockwise
    {
        if (angle_diff < 0)
            angle_diff += 2.0f * M_PI;
    }

    // 4. Calculate arc parameters
    float arc_length = radius * fabsf(angle_diff);
    float segment_mm = 1.0f; // 1mm segments
    uint32_t num_segments = (uint32_t)ceilf(arc_length / segment_mm);

    if (num_segments == 0)
        num_segments = 1;

    // 5. Precalculate Z and E interpolation
    float z_start = motors[MOTOR_Z].position;
    float e_start = motors[MOTOR_E].position;
    float dz = arc->target_z - z_start;
    float de = arc->moveE - e_start;

    // --- TRUE KINEMATIC ACCELERATION CONFIGURATION (By Segment) ---
    float v_max = (float)arc->feed_rate_hz;
    float v_min = (float)arc->feed_rate_hz;
    if (v_max < v_min)
        v_max = v_min;

    float accel_rate_steps = 10000.0f;
    // Approximate segments per second squared (Assuming max axis StepPerMM as the baseline)
    float accel_rate_segments = accel_rate_steps / (StepPerMM[0] * segment_mm);

    uint32_t accel_segments = (uint32_t)((v_max * v_max - v_min * v_min) / (2.0f * accel_rate_segments * (StepPerMM[0] * segment_mm)));

    if (accel_segments > num_segments / 2)
    {
        accel_segments = num_segments / 2;
    }
    uint32_t decel_start_seg = num_segments - accel_segments;
    // --------------------------------------------------------------

    rmt_symbol_word_t sym_buffer[NUM_MOTORS][10];
    static float step_error[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // 6. Arc movement loop
    for (uint32_t seg = 0; seg < num_segments; seg++)
    {
        // --- DYNAMIC FEED RATE ---
        uint32_t current_feed_rate = arc->feed_rate_hz;

        if (seg < accel_segments)
        {
            float steps_equivalent = seg * (StepPerMM[0] * segment_mm);
            current_feed_rate = (uint32_t)sqrtf(v_min * v_min + 2.0f * accel_rate_steps * steps_equivalent);
        }
        else if (seg >= decel_start_seg)
        {
            uint32_t segs_from_end = num_segments - seg;
            float steps_equivalent = segs_from_end * (StepPerMM[0] * segment_mm);
            current_feed_rate = (uint32_t)sqrtf(v_min * v_min + 2.0f * accel_rate_steps * steps_equivalent);
        }

        if (current_feed_rate < (uint32_t)v_min)
            current_feed_rate = (uint32_t)v_min;
        if (current_feed_rate > arc->feed_rate_hz)
            current_feed_rate = arc->feed_rate_hz;
        // -------------------------

        float t = (float)seg / num_segments;
        float current_angle = start_angle + angle_diff * t;

        float next_t = (float)(seg + 1) / num_segments;
        float next_angle = start_angle + angle_diff * next_t;

        float current_x = center_x + radius * cosf(current_angle);
        float current_y = center_y + radius * sinf(current_angle);
        float current_z = z_start + dz * t;
        float current_e = e_start + de * t;

        float next_x = center_x + radius * cosf(next_angle);
        float next_y = center_y + radius * sinf(next_angle);
        float next_z = z_start + dz * next_t;
        float next_e = e_start + de * next_t;

        float seg_dx = next_x - current_x;
        float seg_dy = next_y - current_y;
        float seg_dz = next_z - current_z;
        float seg_de = next_e - current_e;

        uint32_t steps[4] = {
            (uint32_t)(fabsf(seg_dx) * StepPerMM[0]),
            (uint32_t)(fabsf(seg_dy) * StepPerMM[1]),
            (uint32_t)(fabsf(seg_dz) * StepPerMM[2]),
            (uint32_t)(fabsf(seg_de) * StepPerMM[3])};

        uint32_t max_steps = 0;
        for (int i = 0; i < 4; i++)
            if (steps[i] > max_steps)
                max_steps = steps[i];

        if (max_steps == 0)
            continue;

        stepper_set_direction(MOTOR_X, (seg_dx >= 0) ? 0 : 1);
        stepper_set_direction(MOTOR_Y, (seg_dy >= 0) ? 0 : 1);
        stepper_set_direction(MOTOR_Z, (seg_dz >= 0) ? 0 : 1);
        stepper_set_direction(MOTOR_E, (seg_de >= 0) ? 0 : 1);

        uint32_t steps_taken = 0;
        const uint32_t chunk_size = 10;

        while (steps_taken < max_steps)
        {
            uint32_t batch = (max_steps - steps_taken < chunk_size) ? (max_steps - steps_taken) : chunk_size;
            bool channel_active[4] = {false, false, false, false};

            for (int i = 0; i < 4; i++)
            {
                if (steps[i] > 0)
                {
                    float ratio = (float)steps[i] / max_steps;
                    float ideal_steps = batch * ratio;
                    float total_steps_owed = ideal_steps + step_error[i];
                    uint32_t axis_steps = (uint32_t)total_steps_owed;
                    step_error[i] = total_steps_owed - (float)axis_steps;

                    if (axis_steps > 0)
                    {
                        uint32_t axis_freq = (uint32_t)((axis_steps * current_feed_rate) / batch);

                        if (axis_freq > 0)
                        {
                            uint32_t ticks = 1000000 / (2 * axis_freq);

                            if (ticks > 32767)
                                ticks = 32767;
                            if (ticks < 3)
                                ticks = 3;

                            for (uint32_t s = 0; s < axis_steps; s++)
                            {
                                sym_buffer[i][s] = (rmt_symbol_word_t){
                                    .duration0 = (uint16_t)ticks,
                                    .level0 = 1,
                                    .duration1 = (uint16_t)ticks,
                                    .level1 = 0};
                            }

                            rmt_transmit_config_t tx_conf = {.loop_count = 0};

                            ESP_ERROR_CHECK(rmt_transmit(
                                motor_channels[i],
                                motor_encoders[i],
                                sym_buffer[i],
                                axis_steps * sizeof(rmt_symbol_word_t),
                                &tx_conf));

                            channel_active[i] = true;
                            motors[i].targetStep += axis_steps;
                        }
                    }
                }
            }

            for (int i = 0; i < 4; i++)
            {
                if (channel_active[i])
                {
                    rmt_tx_wait_all_done(motor_channels[i], -1);
                }
            }
            steps_taken += batch;
        }

        motors[MOTOR_X].position = next_x;
        motors[MOTOR_Y].position = next_y;
        motors[MOTOR_Z].position = next_z;
        motors[MOTOR_E].position = next_e;
    }

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
    head.target_x = motors[MOTOR_X].position;
    head.target_y = motors[MOTOR_Y].position;
    head.target_z = motors[MOTOR_Z].position;

    // Move off of limit switch if on
    ESP_LOGI(TAG, "Starting coordinated homing...");
    head.target_x += 3.0f;
    head.target_y += 3.0f;
    head.target_z += 3.0f;
    head.feed_rate_hz = 5000;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(motors[MOTOR_X].enable_pin, 0);
        gpio_set_level(motors[MOTOR_Y].enable_pin, 0);
        gpio_set_level(motors[MOTOR_Z].enable_pin, 0);
        xSemaphoreGive(i2c_mutex);
    }
    coordinated_move(&head);

    // Prepare for homing moves

    // ----------------------------------------------------------------
    // HOME X AXIS
    // ----------------------------------------------------------------
    // Flush any stale switch triggers before starting the loop
    xSemaphoreTake(xSwitchSemaphore, 0);
    while (!axis_homed[MOTOR_X])
    {
        head.target_x -= 1.0f;

        // 2. CRITICAL: Actually command the motor to move!
        coordinated_move(&head);

        // Check if the switch triggered during that 1mm move
        if (xSemaphoreTake(xSwitchSemaphore, 0) == pdTRUE)
        {
            motors[MOTOR_X].position = 0.0f;
            head.target_x = 0.0f; // Reset target so Y/Z moves don't go crazy
            axis_homed[MOTOR_X] = true;
            if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
            {
                gpio_set_level(motors[MOTOR_X].enable_pin, 1);
                xSemaphoreGive(i2c_mutex);
            }
            ESP_LOGI(TAG, "X axis homed");
        }
    }

    // ----------------------------------------------------------------
    // HOME Y AXIS
    // ----------------------------------------------------------------
    xSemaphoreTake(ySwitchSemaphore, 0);
    while (!axis_homed[MOTOR_Y])
    {
        head.target_y -= 1.0f;

        coordinated_move(&head); // Command the move
        if (xSemaphoreTake(ySwitchSemaphore, 0) == pdTRUE)
        {
            motors[MOTOR_Y].position = 0.0f;
            head.target_y = 0.0f;
            if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
            {
                gpio_set_level(motors[MOTOR_Y].enable_pin, 1);
                xSemaphoreGive(i2c_mutex);
            }
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
        head.target_z -= 1.0f;

        coordinated_move(&head); // Command the move

        if (xSemaphoreTake(zSwitchSemaphore, 0) == pdTRUE)
        {
            motors[MOTOR_Z].position = 0.0f;
            head.target_z = 0.0f;
            axis_homed[MOTOR_Z] = true;
            if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
            {
                gpio_set_level(motors[MOTOR_Z].enable_pin, 1);
                xSemaphoreGive(i2c_mutex);
            }
            ESP_LOGI(TAG, "Z axis homed");
        }
    }

    // Move to offset position

    head.target_z = 60.0f;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(motors[MOTOR_Z].enable_pin, 0);
        xSemaphoreGive(i2c_mutex);
    }
    coordinated_move(&head);
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(motors[MOTOR_X].enable_pin, 0);
        gpio_set_level(motors[MOTOR_Y].enable_pin, 0);
        xSemaphoreGive(i2c_mutex);
    }
    head.target_x = 29.0f;
    head.target_y = 242.0f;
    coordinated_move(&head);

    // // Set actual home
    motors[MOTOR_X].position = 0.0f;
    motors[MOTOR_Y].position = 200.0f;
    // motors[MOTOR_Z].position = 25.0f;
    head.target_x = 0.0f;
    head.target_y = 200.0f;

    // initalize extruder at position 0
    motors[MOTOR_E].position = 0;

    // Reset step counters after homing to establish new reference frame
    // for (int i = 0; i < NUM_MOTORS; i++)
    //     motors[i].targetStep = 0;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(motors[MOTOR_X].enable_pin, 1);
        gpio_set_level(motors[MOTOR_Y].enable_pin, 1);
        gpio_set_level(motors[MOTOR_Z].enable_pin, 1);
        xSemaphoreGive(i2c_mutex);
    }
    ESP_LOGI(TAG, "Homed");
    return ESP_OK;
}

void position()
{
    printf("MY MOTORS ARE AT X: %.3f, Y: %.3f, Z: %.3f, E:%.3f", motors[MOTOR_X].position, motors[MOTOR_Y].position, motors[MOTOR_Z].position, motors[MOTOR_E].position);
}