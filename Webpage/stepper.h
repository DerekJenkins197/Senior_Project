#ifndef __STEPPER_H
#define __STEPPER_H
 
#include <stdint.h>
 
/* SAA1042 stepper driver connections (Pico W)
 *   GP22 (pin 29) -> Pin 7  (Clock)   one pulse per step
 *   GP21 (pin 27) -> Pin 10 (CW/CCW)
 *   GND  (pin 28) -> Pin 9  (Gnd, must be shared with the driver)
 */
#define STEPPER_CLOCK_PIN   22
#define STEPPER_DIR_PIN     21
 
/* Abovehill 8mm micro stepper: 18 degrees per full step (20 steps/rev).
 * HALF_STEP must match the wiring of SAA1042 pin 8 (high = half step).
 */
#define STEPS_PER_REV       20
#define HALF_STEP           1
#define STEPPER_TRIGGER_CW  1       // 1 = CW, 0 = CCW
 
#define STEP_PERIOD_MS      100     // time between step pulses
#define STEP_PULSE_WIDTH_MS 10      // high time of each step pulse
 
void stepper_init(void);
void stepper_set_direction(uint8_t clockwise);
void stepper_start(void);           // begin stepping (until stepper_stop)
void stepper_stop(void);            // the pulse in progress finishes first
void stepper_poll(void);            // call often from the main loop
 
#endif /* __STEPPER_H */
 