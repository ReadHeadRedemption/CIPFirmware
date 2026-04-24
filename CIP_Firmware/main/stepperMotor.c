#include "stepperMotor.h"
#include "esp_log.h"

static const char *TAG = "STEPPER";

// MCPWM handles
static mcpwm_timer_handle_t timer = NULL;
static mcpwm_oper_handle_t op[2] = {NULL, NULL};  // We will use operator 0 for X/Z and operator 1 for Y/E  
static mcpwm_cmpr_handle_t comparators[NUM_MOTORS] = {NULL};

// Motor configurations
static motor_config_t motors[NUM_MOTORS] = {
    // X Motor: MCPWM Generator 0 (PWM0)
    {X_STEP, X_DIR, 0, NULL},
    // Y Motor: MCPWM Generator 1 (PWM1)
    {Y_STEP, Y_DIR, 0, NULL},
    // Z Motor: MCPWM Generator 2 (PWM2)
    {Z_STEP, Z_DIR, 0, NULL},
    // E Motor: MCPWM Generator 0B (PWM0B)
    {EXTRUDER_STEP, EXTRUDER_DIR, 0, NULL},
};

esp_err_t stepper_motor_init(void)
{
    ESP_LOGI(TAG, "Initializing stepper motors...");
    esp_err_t err = ESP_OK;

    // Configure MCPWM timer
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1 MHz resolution
        .period_ticks = 50,        // 20 kHz default (1MHz / 50 = 20kHz)
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    err = mcpwm_new_timer(&timer_config, &timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MCPWM timer: %s", esp_err_to_name(err));
        return err;
    }


    // Configure MCPWM operator
    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    err = mcpwm_new_operator(&operator_config, &op[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MCPWM operator: %s", esp_err_to_name(err));
        return err;
    }
    err = mcpwm_new_operator(&operator_config, &op[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MCPWM operator: %s", esp_err_to_name(err));
        return err;
    }

    // Connect timer to operator
    err = mcpwm_operator_connect_timer(op[0], timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect timer to operator: %s", esp_err_to_name(err));
        return err;
    }
    err = mcpwm_operator_connect_timer(op[1], timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect timer to operator: %s", esp_err_to_name(err));
        return err;
    }

    // Create comparators and generators for each motor
    uint32_t step_pins[] = {X_STEP, Y_STEP, Z_STEP, EXTRUDER_STEP};
    
    for (int i = 0; i < NUM_MOTORS; i++) {
        // Create comparator
        mcpwm_comparator_config_t comp_config = {
            .flags.update_cmp_on_tez = true,
        };
        err = mcpwm_new_comparator(op[i % 2], &comp_config, &comparators[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create comparator %d: %s", i, esp_err_to_name(err));
            return err;
        }


        // Create generator
        mcpwm_generator_config_t gen_config = {
            .gen_gpio_num = step_pins[i],
        };

        err = mcpwm_new_generator(op[i % 2], &gen_config, &motors[i].generator);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create generator %d: %s", i, esp_err_to_name(err));
            return err;
        }
        // Set generator action: set high on timer zero, set low on compare match
        mcpwm_gen_timer_event_action_t timer_event = {
            .direction = MCPWM_TIMER_DIRECTION_UP,
            .event = MCPWM_TIMER_EVENT_FULL,
            .action = MCPWM_GEN_ACTION_HIGH,
        };
        err = mcpwm_generator_set_action_on_timer_event(motors[i].generator, timer_event);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set generator action on timer event: %s", esp_err_to_name(err));
            return err;
        }

        // Set generator action: set low on compare match
        mcpwm_gen_compare_event_action_t compare_event = {
            .direction = MCPWM_TIMER_DIRECTION_UP,
            .comparator = comparators[i],
            .action = MCPWM_GEN_ACTION_LOW,
        };
        err = mcpwm_generator_set_action_on_compare_event(motors[i].generator, compare_event);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set generator action on compare event: %s", esp_err_to_name(err));
            return err;
        }

        // Set initial compare value (50% duty)
        err = mcpwm_comparator_set_compare_value(comparators[i], 25);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set comparator value: %s", esp_err_to_name(err));
            return err;
        }
    }

    // Initialize direction GPIO pins as outputs
    gpio_config_t gpio_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << X_DIR) | (1ULL << Y_DIR) | (1ULL << Z_DIR) | (1ULL << EXTRUDER_DIR),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    err = gpio_config(&gpio_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DIR GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Enable the timer before starting it
    err = mcpwm_timer_enable(timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer: %s", esp_err_to_name(err));
        return err;
    }

    // Start the timer (use true to enable, false to disable)
    err = mcpwm_timer_start_stop(timer, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize all motors with 0 frequency (stopped)
    for (int i = 0; i < NUM_MOTORS; i++) {
        stepper_set_frequency(i, 0);
        stepper_set_direction(i, 0);
    }

    ESP_LOGI(TAG, "Stepper motors initialized successfully");
    return ESP_OK;
}

esp_err_t stepper_set_frequency(motor_id_t motor_id, uint32_t frequency_hz)
{
    if (motor_id >= NUM_MOTORS || timer == NULL || comparators[motor_id] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    if (frequency_hz == 0) {
        // Disable by setting compare value to 0
        err = mcpwm_comparator_set_compare_value(comparators[motor_id], 0);
        ESP_LOGD(TAG, "Motor %d stopped", motor_id);
    } else {
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
        err = mcpwm_timer_set_period(timer, period_ticks);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set timer period: %s", esp_err_to_name(err));
            return err;
        }

        // Set compare value to 50% duty cycle
        uint32_t compare_value = period_ticks / 2;
        err = mcpwm_comparator_set_compare_value(comparators[motor_id], compare_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set compare value: %s", esp_err_to_name(err));
            return err;
        }

        motors[motor_id].frequency_hz = frequency_hz;
        ESP_LOGD(TAG, "Motor %d frequency set to %lu Hz", motor_id, frequency_hz);
    }

    return err;
}

esp_err_t stepper_set_direction(motor_id_t motor_id, uint8_t direction)
{
    if (motor_id >= NUM_MOTORS) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio_set_level(motors[motor_id].dir_pin, direction ? 1 : 0);
    ESP_LOGD(TAG, "Motor %d direction: %s", motor_id, direction ? "REVERSE" : "FORWARD");
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
        return stepper_set_frequency(motor_id, 0);
    }
}