#include "pico/stdlib.h"
#include "stepper.h"
 
static bool     s_running;      // set while a valid RFID tag is on the antenna
static bool     s_clk_high;
static uint32_t s_next_rise;    // ms timestamp of the next rising edge
static uint32_t s_fall_at;      // ms timestamp when the current pulse ends
 
static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
 
void stepper_init(void)
{
    gpio_init(STEPPER_CLOCK_PIN);
    gpio_init(STEPPER_DIR_PIN);
    gpio_set_dir(STEPPER_CLOCK_PIN, GPIO_OUT);
    gpio_set_dir(STEPPER_DIR_PIN, GPIO_OUT);
    gpio_disable_pulls(STEPPER_CLOCK_PIN);
    gpio_disable_pulls(STEPPER_DIR_PIN);
    gpio_set_drive_strength(STEPPER_CLOCK_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(STEPPER_DIR_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(STEPPER_CLOCK_PIN, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(STEPPER_DIR_PIN, GPIO_SLEW_RATE_FAST);
    gpio_put(STEPPER_CLOCK_PIN, 0);
    stepper_set_direction(STEPPER_TRIGGER_CW);      // set while the clock is low
}
 
// SAA1042 CW/CCW: low = clockwise, high = counter-clockwise
void stepper_set_direction(uint8_t clockwise)
{
    gpio_put(STEPPER_DIR_PIN, !clockwise);
}
 
void stepper_start(void)
{
    if (!s_running) {
        s_running = true;
        s_next_rise = now_ms();
    }
}
 
void stepper_stop(void)
{
    s_running = false;
}
 
void stepper_poll(void)
{
    uint32_t now = now_ms();
 
    if (s_clk_high && (int32_t)(now - s_fall_at) >= 0) {
        gpio_put(STEPPER_CLOCK_PIN, 0);
        s_clk_high = false;
    }
 
    if (s_running && !s_clk_high && (int32_t)(now - s_next_rise) >= 0) {
        gpio_put(STEPPER_CLOCK_PIN, 1);
        s_clk_high = true;
        s_fall_at = now + STEP_PULSE_WIDTH_MS;
        s_next_rise += STEP_PERIOD_MS;
        if ((int32_t)(now - s_next_rise) > 0) {     // fell far behind (e.g. a flash write): don't burst
            s_next_rise = now + STEP_PERIOD_MS;
        }
    }
}
 