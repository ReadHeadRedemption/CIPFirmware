#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// GPIO pin definitions
#define GPIO_PIN_D13 GPIO_NUM_13
#define GPIO_PIN_D12 GPIO_NUM_12
#define GPIO_PIN_D14 GPIO_NUM_14
#define GPIO_PIN_D27 GPIO_NUM_27

// Delay in milliseconds (500ms = half second)
#define TOGGLE_DELAY_MS 500

void app_main(void)
{
    // Configure GPIO pins as outputs
    gpio_config_t io_conf = {
                   // Set as output
        io_conf.pin_bit_mask = (1ULL << GPIO_PIN_D13) |  // D13
                            (1ULL << GPIO_PIN_D12) |  // D12
                            (1ULL << GPIO_PIN_D14) |  // D14
                            (1ULL << GPIO_PIN_D27),   // D27
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE,
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE,
        io_conf.intr_type = GPIO_INTR_DISABLE,        // Disable interrupt
        io_conf.mode = GPIO_MODE_OUTPUT,
    };
   
    
    gpio_config(&io_conf);
    
    printf("GPIO toggling initialized on D13, D12, D14, D27\n");
    
    // Toggle GPIO pins every half second
    uint8_t state = 0;
    while (1) {
        state = !state;  // Toggle state (0 to 1, 1 to 0)
        
        // Set all pins to the same state
        gpio_set_level(GPIO_PIN_D13, state);
        gpio_set_level(GPIO_PIN_D12, state);
        gpio_set_level(GPIO_PIN_D14, state);
        gpio_set_level(GPIO_PIN_D27, state);
        
        printf("GPIO state: %d\n", state);
        
        // Delay for 500ms
        vTaskDelay(pdMS_TO_TICKS(TOGGLE_DELAY_MS));
    }
}
