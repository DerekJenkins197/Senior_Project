#ifndef __STEPPER_H
#define __STEPPER_H

#include <stdint.h>

/* SAA1042 stepper driver connections (Pico W)
 *   GP1 (pin 2) -> Pin 7  (Clock)   one pulse per step
 *   GP2 (pin 4) -> Pin 10 (CW/CCW)
 *   GND (pin 3) -> Pin 9  (Gnd, must be shared with the driver)
 */
#define STEPPER_CLOCK_PIN   1
#define STEPPER_DIR_PIN     2

/* Abovehill 8mm micro stepper: 18 degrees per full step (20 steps/rev).
 * HALF_STEP must match the wiring of SAA1042 pin 8 (high = half step).
 */
#define STEPS_PER_REV       20
#define HALF_STEP           1
#define STEPS_PER_TRIGGER   1       // steps taken for each valid RFID read
#define STEPPER_TRIGGER_CW  1       // direction of those steps: 1 = CW, 0 = CCW

#define STEP_PERIOD_MS      100     // time between step pulses
#define STEP_PULSE_WIDTH_MS 10      // high time of each step pulse

#define STEPPER_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)
#define STEPPER_TASK_STACK      configMINIMAL_STACK_SIZE
#define LED_TASK_PRIORITY       (tskIDLE_PRIORITY + 1)      // below the stepper
#define LED_TASK_STACK          (configMINIMAL_STACK_SIZE * 2)
#define LED_HEARTBEAT_MS        500     // LED toggle period: one blink per second
#define LED_RX_HOLD_MS          200     // LED held on this long after each RFID UART byte
#define LED_POLL_MS             10

void stepper_init(void);
void stepper_set_direction(uint8_t clockwise);
void stepper_task(void *params);
void stepper_trigger(void);
void led_rx_activity(void);

#endif /* __STEPPER_H */
