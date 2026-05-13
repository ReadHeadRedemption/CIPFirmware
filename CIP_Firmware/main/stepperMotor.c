#include "stepperMotor.h"
#include "esp_log.h"

static const char *TAG = "STEPPER";

static portMUX_TYPE stepperMux = portMUX_INITIALIZER_UNLOCKED;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                MOTOR INITIALIZATION
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MCPWM handles
static mcpwm_timer_handle_t timer[NUM_MOTORS] = {NULL}; 
static mcpwm_oper_handle_t op[NUM_MOTORS] = {NULL};  
static mcpwm_cmpr_handle_t comparators[NUM_MOTORS] = {NULL};

// Motor configurations
static motor_config_t motors[NUM_MOTORS] = {
    // X Motor: MCPWM Generator 0 (PWM0)
    {xStep, xDir, 0, NULL, 0, 0, tempEnable},
    // Y Motor: MCPWM Generator 1 (PWM1)
    {yStep, yDir, 0, NULL, 0, 0, tempEnable},
    // Z Motor: MCPWM Generator 2 (PWM2)
    {zStep, zDir, 0, NULL, 0, 0, tempEnable},
    // E Motor: MCPWM Generator 0B (PWM0B)
    {eStep, eDir, 0, NULL, 0, 0, tempEnable},
};


// Interrupt for step counter
// ISR Callback: Fires every time the MCPWM timer hits 0 (pulse starts)
static bool IRAM_ATTR mcpwm_timer_empty_cb(mcpwm_timer_handle_t timer, const mcpwm_timer_event_data_t *edata, void *user_ctx) {
    int motor_id = (int)(intptr_t)user_ctx; 
    
    if (motors[motor_id].targetStep > 0) {
        motors[motor_id].targetStep--;
        
        // If we just hit the final step, instantly cut the PWM signal
        if (motors[motor_id].targetStep == 0) {
            mcpwm_generator_set_force_level(motors[motor_id].generator, 0, true);
        }
    }
    return false; 
}

esp_err_t stepper_motor_init(void)
{
    ESP_LOGI(TAG, "Initializing stepper motors...");
    esp_err_t err = ESP_OK;
    uint32_t step_pins[] = {xStep, yStep, zStep, eStep};

    ///////////////////////////////////////////////////////////////////////////////////////////////
    for (int i = 0; i < NUM_MOTORS; i++) {
        int group_id = (i < 3) ? 0 : 1; // Motors 0,1,2 in Group 0. Motor 3 in Group 1.
        // Create timer for each motor
        mcpwm_timer_config_t timer_config = {
            .group_id = group_id,
            .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,
            .period_ticks = 50,
            .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        };
        err = mcpwm_new_timer(&timer_config, &timer[i]);
        if (err != ESP_OK) return err;

        // --- NEW CODE: REGISTER THE INTERRUPT CALLBACK ---
        mcpwm_timer_event_callbacks_t cbs = {
            .on_empty = mcpwm_timer_empty_cb,
        };
        // Pass 'i' as the context so the ISR knows which motor to increment
        err = mcpwm_timer_register_event_callbacks(timer[i], &cbs, (void *)(intptr_t)i);
        if (err != ESP_OK) return err;
        // --------------------------------------------------

        // Creating Operators for each motor
        mcpwm_operator_config_t operator_config = {
            .group_id = group_id,
        };
        err = mcpwm_new_operator(&operator_config, &op[i]);
        if (err != ESP_OK) return err;

        err = mcpwm_operator_connect_timer(op[i], timer[i]);
        if (err != ESP_OK) return err;
        // Create Comparator for each motor
        mcpwm_comparator_config_t comp_config = {
            .flags.update_cmp_on_tez = true,
        };
        // FIXED: Changed op[j/2] to op[i]
        err = mcpwm_new_comparator(op[i], &comp_config, &comparators[i]); 
        if (err != ESP_OK) return err;

        // 4. Create Generator
        mcpwm_generator_config_t gen_config = {
            .gen_gpio_num = step_pins[i],
        };
        // FIXED: Changed op[j/2] to op[i]
        err = mcpwm_new_generator(op[i], &gen_config, &motors[i].generator); 
        if (err != ESP_OK) return err;

        // 5. Set Generator Actions
        mcpwm_gen_timer_event_action_t timer_event = {
            .direction = MCPWM_TIMER_DIRECTION_UP,
            .event = MCPWM_TIMER_EVENT_EMPTY,
            .action = MCPWM_GEN_ACTION_HIGH,
        };
        err = mcpwm_generator_set_action_on_timer_event(motors[i].generator, timer_event);
        if (err != ESP_OK) return err;

        mcpwm_gen_compare_event_action_t compare_event = {
            .direction = MCPWM_TIMER_DIRECTION_UP,
            .comparator = comparators[i],
            .action = MCPWM_GEN_ACTION_LOW,
        };
        err = mcpwm_generator_set_action_on_compare_event(motors[i].generator, compare_event);
        if (err != ESP_OK) return err;

        // 6. Set initial compare value (50% duty)
        err = mcpwm_comparator_set_compare_value(comparators[i], 25);
        if (err != ESP_OK) return err;

        // 7. Start each timer continuously
        err = mcpwm_timer_enable(timer[i]);
        if (err != ESP_OK) return err;
        
        err = mcpwm_timer_start_stop(timer[i], MCPWM_TIMER_START_NO_STOP);
        if (err != ESP_OK) return err;
    }

    // Initialize all motors with 0 frequency (stopped)
    for (int i = 0; i < NUM_MOTORS; i++) {
        stepper_set_frequency(i, 0);
        stepper_set_direction(i, 0);
    }

    ESP_LOGI(TAG, "Stepper motors initialized successfully");
    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                MOTOR CONTROL FUNCTIONS
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


esp_err_t stepper_set_frequency(motor_id_t motor_id, uint32_t frequency_hz)
{   
    if (motor_id >= NUM_MOTORS || timer[motor_id] == NULL || comparators[motor_id] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    if (frequency_hz == 0) {
        // Force output low to stop immediately
        err = mcpwm_generator_set_force_level(motors[motor_id].generator, 0, true);
        motors[motor_id].frequency_hz = 0;
        ESP_LOGD(TAG, "Motor %d stopped", motor_id);
    } else {
        // Release the forced low state if it was stopped
        mcpwm_generator_set_force_level(motors[motor_id].generator, -1, true); 

        // CRITICAL FIX: Only update the hardware registers if the frequency actually changed.
        // Constantly updating the period resets the timer and prevents slow pulses from firing.
        if (motors[motor_id].frequency_hz != frequency_hz) {
            uint32_t period_ticks = 1000000 / frequency_hz;
            
            // Clamp to reasonable values (100kHz max, 20Hz min)
            if (period_ticks < 10) period_ticks = 10;  
            if (period_ticks > 50000) period_ticks = 50000;

            err = mcpwm_timer_set_period(timer[motor_id], period_ticks);
            if (err != ESP_OK) return err;

            err = mcpwm_comparator_set_compare_value(comparators[motor_id], period_ticks / 2);
            if (err != ESP_OK) return err;

            motors[motor_id].frequency_hz = frequency_hz;
            ESP_LOGI(TAG, "Motor %d frequency set to %lu Hz", motor_id, frequency_hz);
        }
    }
    return err;
}

esp_err_t stepper_set_direction(motor_id_t motor_id, uint8_t direction)
{
    if (motor_id >= NUM_MOTORS) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio_set_level(motors[motor_id].dir_pin, direction ? 1 : 0);
    ESP_LOGI(TAG, "Motor %d direction: %s", motor_id, direction ? "REVERSE" : "FORWARD");
    return err;
}

esp_err_t stepper_enable(motor_id_t motor_id, uint8_t enable)
{
    if (motor_id >= NUM_MOTORS) {
        return ESP_ERR_INVALID_ARG;
    }

    if (enable) {
        // Re-enable by setting frequency to current value
        return stepper_set_frequency(motor_id, motors[motor_id].frequency_hz);
    } else {
        // Disable by setting frequency to 0
        // eventually change for enable pin
        return stepper_set_frequency(motor_id, 0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                MOTOR MOVEMENT FUNCTIONS
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Move to absolute position in mm
float StepPerMM[] = {xStepsPerMM, yStepsPerMM, zStepsPerMM, eStepsPerMM};

esp_err_t coordinated_move(MoveCmd_t * move)
{
    // 1. Calculate deltas and steps
    float delta_x = fabs(move->target_x - motors[MOTOR_X].position);
    float delta_y = fabs(move->target_y - motors[MOTOR_Y].position);
    float delta_z = fabs(move->target_z - motors[MOTOR_Z].position);

    uint32_t steps_x = (uint32_t)(delta_x * StepPerMM[MOTOR_X]);
    uint32_t steps_y = (uint32_t)(delta_y * StepPerMM[MOTOR_Y]);
    uint32_t steps_z = (uint32_t)(delta_z * StepPerMM[MOTOR_Z]);

    if (steps_x == 0 && steps_y == 0 && steps_z == 0) return ESP_OK;

    // 2. Identify lead axis
    uint32_t max_steps = steps_x;
    if (steps_y > max_steps) max_steps = steps_y;
    if (steps_z > max_steps) max_steps = steps_z;

    // 3. Pre-calculate ratios
    float ratio_x = (float)steps_x / max_steps;
    float ratio_y = (float)steps_y / max_steps;
    float ratio_z = (float)steps_z / max_steps;

    // 4. Set Directions and Initialize Step Counters
    stepper_set_direction(MOTOR_X, (move->target_x >= motors[MOTOR_X].position) ? 0 : 1);
    stepper_set_direction(MOTOR_Y, (move->target_y >= motors[MOTOR_Y].position) ? 0 : 1);
    stepper_set_direction(MOTOR_Z, (move->target_z >= motors[MOTOR_Z].position) ? 0 : 1);

    // Disable interrupts briefly while loading new volatile variables to prevent race conditions
    portENTER_CRITICAL(&stepperMux);
    motors[MOTOR_X].targetStep = steps_x;
    motors[MOTOR_Y].targetStep = steps_y;
    motors[MOTOR_Z].targetStep = steps_z;
    portEXIT_CRITICAL(&stepperMux);

    //5. Acceleration Profile Config
    uint32_t min_freq = 200; 
    uint32_t current_speed = min_freq;
    uint32_t accel_step = 80; 
    
    uint32_t decel_threshold = (max_steps / 5 > 200) ? (max_steps / 5) : 200;
    if (decel_threshold > max_steps) decel_threshold = max_steps / 2;

    // 6. Monitoring Loop
    while (motors[MOTOR_X].targetStep > 0 || motors[MOTOR_Y].targetStep > 0 || motors[MOTOR_Z].targetStep > 0) 
    {
        uint32_t lead_remaining = 0;
        if (max_steps == steps_x) lead_remaining = motors[MOTOR_X].targetStep;
        else if (max_steps == steps_y) lead_remaining = motors[MOTOR_Y].targetStep;
        else lead_remaining = motors[MOTOR_Z].targetStep;

        // Acceleration / Deceleration Logic
        if (lead_remaining > decel_threshold) {
            // Ramp Up
            if (current_speed < move->feed_rate_hz) {
                current_speed += accel_step;
                if (current_speed > move->feed_rate_hz) current_speed = move->feed_rate_hz;
            }
        } else {
            // Ramp Down
            if (current_speed > min_freq) {
                current_speed -= (accel_step + 20); 
                if (current_speed < min_freq) current_speed = min_freq;
            }
        }

        // --- THE FIX: Use current_speed, not move->feed_rate_hz ---
        
        // Apply speeds. If the motor is out of steps, turn it off.
        if (motors[MOTOR_X].targetStep > 0) stepper_set_frequency(MOTOR_X, (uint32_t)(current_speed * ratio_x));
        else stepper_set_frequency(MOTOR_X, 0);

        if (motors[MOTOR_Y].targetStep > 0) stepper_set_frequency(MOTOR_Y, (uint32_t)(current_speed * ratio_y));
        else stepper_set_frequency(MOTOR_Y, 0);

        if (motors[MOTOR_Z].targetStep > 0) stepper_set_frequency(MOTOR_Z, (uint32_t)(current_speed * ratio_z));
        else stepper_set_frequency(MOTOR_Z, 0);

    }

    // 7. Finalize
    stepper_set_frequency(MOTOR_X, 0);
    stepper_set_frequency(MOTOR_Y, 0);
    stepper_set_frequency(MOTOR_Z, 0);

    motors[MOTOR_X].position = move->target_x;
    motors[MOTOR_Y].position = move->target_y;
    motors[MOTOR_Z].position = move->target_z;

    return ESP_OK;
}