#include "stepperMotor.h"
#include "esp_log.h"

static const char *TAG = "STEPPER";

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
    // We pass the loop index 'i' as the user_ctx so we know which motor fired
    int motor_id = (int)(intptr_t)user_ctx; 
    
    // Increment the step counter
    motors[motor_id].targetStep--;
    
    // Return false (return true only if you are waking a higher-priority FreeRTOS task here)
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
    ESP_LOGI(TAG, "Stepper motor %d: Setting frequency to %lu Hz", motor_id, frequency_hz);
    if (motor_id >= NUM_MOTORS || timer[motor_id] == NULL || comparators[motor_id] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;
    //ESP_LOGI(TAG, "Test point 1");
    if (frequency_hz == 0) {
        //ESP_LOGI(TAG, "Test point 2");
        // Disable by setting compare value to 0
        err = mcpwm_comparator_set_compare_value(comparators[motor_id], 0);
        ESP_LOGD(TAG, "Motor %d stopped", motor_id);
    } else {
        ESP_LOGD(TAG, "Motor %d started", motor_id);
        // Calculate compare value for 50% duty cycle
        // period_ticks = 1MHz / frequency_hz
        uint32_t period_ticks = 1000000 / frequency_hz;
        
        // Clamp to reasonable values
        if (period_ticks < 10) {
            period_ticks = 10;  // Minimum 100 kHz
        }
        if (period_ticks > 50000) {
            period_ticks = 50000;  // Maximum 20 Hz
        }

        // Update timer period using mcpwm_timer_set_period
        err = mcpwm_timer_set_period(timer[motor_id], period_ticks);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set timer period: %s", esp_err_to_name(err));
            return err;
        }
        //ESP_LOGI(TAG, "Test point 3");
        // Set compare value to 50% duty cycle
        uint32_t compare_value = period_ticks / 2;
        err = mcpwm_comparator_set_compare_value(comparators[motor_id], compare_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set compare value: %s", esp_err_to_name(err));
            return err;
        }

        motors[motor_id].frequency_hz = frequency_hz;
        ESP_LOGI(TAG, "Motor %d frequency set to %lu Hz", motor_id, frequency_hz);
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
// Non-blocking: Starts the motor instantly at the starting frequency
esp_err_t stepper_start_move(motor_id_t motor_id, uint32_t steps, uint8_t direction, uint32_t start_freq)
{
    if (motor_id >= NUM_MOTORS) return ESP_ERR_INVALID_ARG;

    stepper_set_direction(motor_id, direction);
    motors[motor_id].targetStep = steps;
    
    // Start at a low frequency (e.g., 500Hz)
    stepper_set_frequency(motor_id, start_freq); 
    
    return ESP_OK;
}


// Move to absolute position in mm
// Array of step to mm X, Y, Z, E
float StepPerMM[] = {100,100,400,200};

esp_err_t coordinated_move(MoveCmd_t move)
{
    // 1. Calculate deltas for all axes
    float delta_x = fabs(move.target_x - (float)motors[MOTOR_X].position);
    float delta_y = fabs(move.target_y - motors[MOTOR_Y].position);
    float delta_z = fabs(move.target_z - motors[MOTOR_Z].position);

    uint32_t steps_x = (uint32_t)(delta_x * StepPerMM[MOTOR_X]);
    uint32_t steps_y = (uint32_t)(delta_y * StepPerMM[MOTOR_Y]);
    uint32_t steps_z = (uint32_t)(delta_z * StepPerMM[MOTOR_Z]);

    if (steps_x == 0 && steps_y == 0 && steps_z == 0) return ESP_OK; // No movement needed

    // 2. Find the "lead" axis (the one taking the most steps)
    uint32_t max_steps = steps_x;
    if (steps_y > max_steps) max_steps = steps_y;
    if (steps_z > max_steps) max_steps = steps_z;

    // 3. Calculate proportional frequencies (so they start and stop together)
    uint32_t freq_x = (uint32_t)((float)steps_x / max_steps * move.feed_rate_hz);
    uint32_t freq_y = (uint32_t)((float)steps_y / max_steps * move.feed_rate_hz);
    uint32_t freq_z = (uint32_t)((float)steps_z / max_steps * move.feed_rate_hz);

    // 4. Set Directions and prepare target steps
    if (steps_x > 0) {
        stepper_set_direction(MOTOR_X, (move.target_x > motors[MOTOR_X].position) ? 0 : 1);
        motors[MOTOR_X].targetStep = steps_x;
    }
    if (steps_y > 0) {
        stepper_set_direction(MOTOR_Y, (move.target_y > motors[MOTOR_Y].position) ? 0 : 1);
        motors[MOTOR_Y].targetStep = steps_y;
    }
    if (steps_z > 0) {
        stepper_set_direction(MOTOR_Z, (move.target_z > motors[MOTOR_Z].position) ? 0 : 1);
        motors[MOTOR_Z].targetStep = steps_z;
    }

    // 5. Start all motors simultaneously at a starting safe frequency (no delay between starts!)
    uint32_t start_freq_x = (steps_x > 0) ? 500 : 0;
    uint32_t start_freq_y = (steps_y > 0) ? 500 : 0;
    uint32_t start_freq_z = (steps_z > 0) ? 500 : 0;

    stepper_set_frequency(MOTOR_X, start_freq_x);
    stepper_set_frequency(MOTOR_Y, start_freq_y);
    stepper_set_frequency(MOTOR_Z, start_freq_z);

    // 6. Central Monitoring & Acceleration Loop
    uint32_t current_speed = 500;
    while (motors[MOTOR_X].targetStep > 0 || motors[MOTOR_Y].targetStep > 0 || motors[MOTOR_Z].targetStep > 0) 
    {
        // Acceleration Ramp
        if (current_speed < move.feed_rate_hz) {
            current_speed += 100; // Linear ramp up 100Hz every 10ms (Adjust rate to taste)
            if (current_speed > move.feed_rate_hz) current_speed = move.feed_rate_hz;
            
            // Adjust proportional speeds
            if (steps_x > 0) stepper_set_frequency(MOTOR_X, (uint32_t)((float)steps_x / max_steps * current_speed));
            if (steps_y > 0) stepper_set_frequency(MOTOR_Y, (uint32_t)((float)steps_y / max_steps * current_speed));
            if (steps_z > 0) stepper_set_frequency(MOTOR_Z, (uint32_t)((float)steps_z / max_steps * current_speed));
        }

        // Deceleration Ramp: Check if lead axis is nearing target
        uint32_t lead_remaining = 0;
        if (max_steps == steps_x) lead_remaining = motors[MOTOR_X].targetStep;
        else if (max_steps == steps_y) lead_remaining = motors[MOTOR_Y].targetStep;
        else lead_remaining = motors[MOTOR_Z].targetStep;

        if (lead_remaining < 300 && current_speed > 500) {
            current_speed -= 150; // Decelerate fast as we approach target
            if (current_speed < 500) current_speed = 500;

            if (steps_x > 0) stepper_set_frequency(MOTOR_X, (uint32_t)((float)steps_x / max_steps * current_speed));
            if (steps_y > 0) stepper_set_frequency(MOTOR_Y, (uint32_t)((float)steps_y / max_steps * current_speed));
            if (steps_z > 0) stepper_set_frequency(MOTOR_Z, (uint32_t)((float)steps_z / max_steps * current_speed));
        }

        // Check if an individual motor finished its steps & stop its output
        if (steps_x > 0 && motors[MOTOR_X].targetStep == 0) stepper_set_frequency(MOTOR_X, 0);
        if (steps_y > 0 && motors[MOTOR_Y].targetStep == 0) stepper_set_frequency(MOTOR_Y, 0);
        if (steps_z > 0 && motors[MOTOR_Z].targetStep == 0) stepper_set_frequency(MOTOR_Z, 0);

        vTaskDelay(pdMS_TO_TICKS(10)); // single 10ms check window for ALL motors
    }

    // 7. Cleanup & Update Position
    stepper_set_frequency(MOTOR_X, 0);
    stepper_set_frequency(MOTOR_Y, 0);
    stepper_set_frequency(MOTOR_Z, 0);

    motors[MOTOR_X].position = move.target_x;
    motors[MOTOR_Y].position = move.target_y;
    motors[MOTOR_Z].position = move.target_z;

    return ESP_OK;
}