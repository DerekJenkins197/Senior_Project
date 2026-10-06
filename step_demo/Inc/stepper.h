#ifndef __STEPPER_H
#define __STEPPER_H

#include <stdint.h>

/* SAA1042 stepper driver connections
 *   PC0 -> Pin 7  (Clock)   one pulse per step
 *   PC3 -> Pin 10 (CW/CCW)
 *   GND -> Pin 9  (Gnd, must be shared with the driver)
 */

/* Abovehill 8mm micro stepper: 18 degrees per full step (20 steps/rev).
 * HALF_STEP must match the wiring of SAA1042 pin 8 (high = half step).
 */
#define STEPS_PER_REV       20
#define HALF_STEP           1
#define SWEEP_DEGREES       90      // turn this far, then reverse back
#define STEPS_PER_SWEEP     ((STEPS_PER_REV * (HALF_STEP + 1) * SWEEP_DEGREES) / 360)

#define SWEEP_TIME_MS       1000    // time to complete one sweep
#define STEP_PERIOD_MS      (SWEEP_TIME_MS / STEPS_PER_SWEEP)  // time between step pulses
#define STEP_PULSE_WIDTH_MS 10      // high time of each step pulse

void stepper_init(void);
void stepper_set_direction(uint8_t clockwise);
void stepper_step_done(void);

#endif /* __STEPPER_H */
