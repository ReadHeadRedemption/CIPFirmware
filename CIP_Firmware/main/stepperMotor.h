#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include "esp_err.h"
#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "pins.h"

// Motor identifiers
typedef enum {
    MOTOR_X = 0,
    MOTOR_Y = 1,
    MOTOR_Z = 2,
    MOTOR_E = 3,
    NUM_MOTORS
} motor_id_t;

// Motor configuration
typedef struct {
    uint32_t step_pin;
    uint32_t dir_pin;
    uint32_t frequency_hz;    // Current frequency (steps/sec)
    mcpwm_gen_handle_t generator;  // MCPWM generator handle
} motor_config_t;

/**
 * @brief Initialize all stepper motors (MCPWM + GPIO)
 * @return ESP_OK on success
 */
esp_err_t stepper_motor_init(void);

/**
 * @brief Set step pulse frequency for a motor
 * @param motor_id: which motor (X, Y, Z, E)
 * @param frequency_hz: target step frequency in Hz
 * @return ESP_OK on success
 */
esp_err_t stepper_set_frequency(motor_id_t motor_id, uint32_t frequency_hz);

/**
 * @brief Set direction for a motor
 * @param motor_id: which motor
 * @param direction: 0 = forward, 1 = reverse
 */
esp_err_t stepper_set_direction(motor_id_t motor_id, uint8_t direction);

/**
 * @brief Enable/disable motor output
 * @param motor_id: which motor
 * @param enable: 1 = enable, 0 = disable
 */
esp_err_t stepper_enable(motor_id_t motor_id, uint8_t enable);

#endif // STEPPER_MOTOR_H