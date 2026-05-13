#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include "esp_err.h"
#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "pins.h"
#include <math.h>

#define MicroStepping 32.0f
#define MotorStepsPerRev (360.0f / 1.8f)

// Calculate steps per mm as compile-time constants
#define xStepsPerMM  ((MotorStepsPerRev * MicroStepping) / (20.0f * 2.0f))   // 20 tooth gear w/ 2mm belt pitch
#define yStepsPerMM  ((MotorStepsPerRev * MicroStepping) / (20.0f * 2.0f))
#define zStepsPerMM  ((MotorStepsPerRev * MicroStepping) / 8.0f)             // 8mm shift per revolution
#define eStepsPerMM  ((MotorStepsPerRev * MicroStepping) / 8.0f)

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
    
    volatile uint32_t targetStep; // Total steps moved (for position tracking)
    int32_t position; // Current position in steps
    uint32_t enable_pin; // GPIO pin for enabling/disabling the motor
    

} motor_config_t;

// Struct to define a multi-axis move command
typedef struct {
    float target_x;
    float target_y;
    float target_z;
    uint32_t feed_rate_hz; // Max speed of the lead axis
} MoveCmd_t;


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

esp_err_t stepper_start_move(motor_id_t motor_id, uint32_t steps, uint8_t direction, uint32_t start_freq);
esp_err_t coordinated_move(MoveCmd_t * move);

#endif // STEPPER_MOTOR_H