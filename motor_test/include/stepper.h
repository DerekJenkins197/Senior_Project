#ifndef __STEPPER_H
#define __STEPPER_H

#include <stdbool.h>
#include <stdint.h>

/* SAA1042 stepper driver connections (Pico W)
 *   GP21 (pin 27) -> Pin 7  (Clock)   one pulse per step
 *   GP20 (pin 26) -> Pin 10 (CW/CCW)
 *   GND           -> Pin 9  (Set) and pins 4, 5, 12, 13 (Gnd, shared with the Pico)
 *   5 V           -> Pin 8  (Full/Half step, high = half step)
 *   5 V           -> Pins 2, 15 (V_M) and pin 11 (V_D)
 *   47k to GND    -> Pin 6  (Bias)
 * Motor coils: Black/Brown on pins 1/3, Orange/Yellow on pins 14/16
 */
#define STEPPER_CLOCK_PIN   21
#define STEPPER_DIR_PIN     20

/* 15 mm 4-wire geared stepper: 18 degrees per full step (20 steps/rev) at
 * the motor, then a 1:35 gearbox, so 1400 half-step pulses per output turn.
 * HALF_STEP must match the wiring of SAA1042 pin 8 (high = half step).
 */
#define STEPS_PER_REV       20
#define HALF_STEP           1
#define GEAR_RATIO          35
#define PULSES_PER_REV      (STEPS_PER_REV * (HALF_STEP + 1) * GEAR_RATIO)   // output shaft

#define STEP_PERIOD_MS_DEFAULT  10      // time between step pulses (10 ms -> 14 s per output rev)
#define STEP_PERIOD_MS_MIN      5
#define STEP_PERIOD_MS_MAX      2000
#define STEP_PULSE_WIDTH_MS     10      // high time of each step pulse, capped at half the period

// Auto-turn (on at boot): AUTO_TURN_DEGREES every AUTO_TURN_PERIOD_MS, always the same way
#define AUTO_TURN_DEGREES       90
#define AUTO_TURN_PULSES        (AUTO_TURN_DEGREES * PULSES_PER_REV / 360)
#define AUTO_TURN_PERIOD_MS     5000    // start of one turn to the start of the next
#define AUTO_TURN_CW            1       // 1 = CW, 0 = CCW

#define STEPPER_CONTINUOUS  INT32_MAX   // stepper_move(+/-STEPPER_CONTINUOUS) runs until stepper_stop()

#define STEPPER_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)
#define STEPPER_TASK_STACK      (configMINIMAL_STACK_SIZE * 2)
#define LED_TASK_PRIORITY       (tskIDLE_PRIORITY + 1)      // below the stepper
#define LED_TASK_STACK          (configMINIMAL_STACK_SIZE * 2)
#define LED_HEARTBEAT_MS        500     // LED toggle period: one blink per second
#define LED_POLL_MS             10

void stepper_init(void);
void stepper_set_direction(uint8_t clockwise);
void stepper_task(void *params);
void stepper_move(int32_t pulses);      // > 0 = CW, < 0 = CCW
void stepper_stop(void);
void stepper_set_period_ms(uint32_t ms);
void stepper_set_auto(bool on);
bool stepper_get_auto(void);
void stepper_print_status(void);

#endif /* __STEPPER_H */
