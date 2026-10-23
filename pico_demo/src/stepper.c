#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "stepper.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"    // Pico W: LED is on the wireless chip, not a GPIO
#endif

static TaskHandle_t led_task_handle;
static TaskHandle_t stepper_task_handle;
static volatile bool stepper_running;     // set while the valid RFID tag is on the antenna

static volatile TickType_t led_rx_tick;   // tick of the last RFID UART byte
static volatile bool led_rx_seen;

/* The LED blinks in its own lower-priority task. On a Pico W each LED write
 * is a command to the wireless chip that can take a long time, so doing it in
 * stepper_task stretched every step period.
 * It toggles every LED_HEARTBEAT_MS to show the code is running, and on
 * each step. While RFID UART bytes are arriving it stays solid on.
 */
static void led_task(void *params)
{
    (void)params;
    bool led_on = false;        // heartbeat state
    bool shown = false;         // what the LED is actually showing

#ifdef CYW43_WL_GPIO_LED_PIN
    hard_assert(cyw43_arch_init() == PICO_OK);              // FreeRTOS cyw43 must start inside a task
#else
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    TickType_t next_beat = xTaskGetTickCount() + pdMS_TO_TICKS(LED_HEARTBEAT_MS);

    while (1) {
        // Wake for a step, or every LED_POLL_MS to check the heartbeat and RX activity
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_POLL_MS)) != 0) {
            led_on = !led_on;
        }
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - next_beat) >= 0) {
            next_beat += pdMS_TO_TICKS(LED_HEARTBEAT_MS);
            led_on = !led_on;
        }

        bool rx_active = led_rx_seen && (now - led_rx_tick) < pdMS_TO_TICKS(LED_RX_HOLD_MS);
        bool want = rx_active || led_on;
        if (want != shown) {                                // only talk to the CYW43 on a change
            shown = want;
#ifdef CYW43_WL_GPIO_LED_PIN
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, shown);
#else
            gpio_put(PICO_DEFAULT_LED_PIN, shown);
#endif
        }
    }
}

// Called by the RFID task whenever UART bytes arrive
void led_rx_activity(void)
{
    led_rx_tick = xTaskGetTickCount();
    led_rx_seen = true;
}

void stepper_init(void)
{
    // Clock and CW/CCW -> push-pull outputs, no pull-up/down
    gpio_init(STEPPER_CLOCK_PIN);
    gpio_init(STEPPER_DIR_PIN);
    gpio_set_dir(STEPPER_CLOCK_PIN, GPIO_OUT);
    gpio_set_dir(STEPPER_DIR_PIN, GPIO_OUT);
    gpio_disable_pulls(STEPPER_CLOCK_PIN);
    gpio_disable_pulls(STEPPER_DIR_PIN);
    // Default is 4 mA / slow slew; drive the SAA1042 inputs as hard as the STM32 did
    gpio_set_drive_strength(STEPPER_CLOCK_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(STEPPER_DIR_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(STEPPER_CLOCK_PIN, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(STEPPER_DIR_PIN, GPIO_SLEW_RATE_FAST);
    gpio_put(STEPPER_CLOCK_PIN, 0);                         // Clock starts low

    // Green LED shows each step
    xTaskCreate(led_task, "led", LED_TASK_STACK, NULL, LED_TASK_PRIORITY, &led_task_handle);
}

// SAA1042 CW/CCW: low = clockwise, high = counter-clockwise
void stepper_set_direction(uint8_t clockwise)
{
    gpio_put(STEPPER_DIR_PIN, !clockwise);
}

// Called by the RFID task when the valid tag arrives: step until stepper_stop()
void stepper_start(void)
{
    stepper_running = true;
    if (stepper_task_handle != NULL) {
        xTaskNotifyGive(stepper_task_handle);
    }
}

// Called by the RFID task when the valid tag is gone; the current step finishes first
void stepper_stop(void)
{
    stepper_running = false;
}

/* Idles until stepper_start(), then pulses the clock until stepper_stop():
 * high for STEP_PULSE_WIDTH_MS, every STEP_PERIOD_MS.
 */
void stepper_task(void *params)
{
    (void)params;
    stepper_task_handle = xTaskGetCurrentTaskHandle();

    // Set direction while the clock is low, before the first rising edge
    stepper_set_direction(STEPPER_TRIGGER_CW);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);            // wait for the valid RFID tag
        TickType_t last_wake = xTaskGetTickCount();

        for (uint32_t step = 1; stepper_running; step++) {
            gpio_put(STEPPER_CLOCK_PIN, 1);                 // Clock pin high
            xTaskNotifyGive(led_task_handle);               // toggle LED each step
            vTaskDelay(pdMS_TO_TICKS(STEP_PULSE_WIDTH_MS));
            gpio_put(STEPPER_CLOCK_PIN, 0);                 // Clock pin low
            printf("%8lu ms  step %lu  %s  dir pin=%d\n", (unsigned long)to_ms_since_boot(get_absolute_time()),
                   (unsigned long)step, STEPPER_TRIGGER_CW ? "CW " : "CCW", gpio_get(STEPPER_DIR_PIN));  // DEBUG
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STEP_PERIOD_MS));
        }
    }
}
