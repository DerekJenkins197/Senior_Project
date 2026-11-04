#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "stepper.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"    // Pico W: LED is on the wireless chip, not a GPIO
#endif

// One request for stepper_task. gen ties it to the command that made it, so a
// later move or stop (which bumps cmd_gen) cuts it short between pulses.
typedef struct {
    int32_t  pulses;            // > 0 = CW, < 0 = CCW
    uint32_t gen;
} move_t;

static TaskHandle_t led_task_handle;
static QueueHandle_t move_queue;

static volatile uint32_t cmd_gen;           // bumped by every stepper_move() / stepper_stop()
static volatile uint32_t step_period_ms = STEP_PERIOD_MS_DEFAULT;
static volatile bool auto_turn = true;      // AUTO_TURN_DEGREES every AUTO_TURN_PERIOD_MS, on at boot
static volatile bool moving;
static volatile bool moving_cw;
static volatile int32_t position;           // pulses, CW positive, since boot

/* The LED blinks in its own lower-priority task. On a Pico W each LED write
 * is a command to the wireless chip that can take a long time, so doing it in
 * stepper_task would stretch every step period.
 * It toggles every LED_HEARTBEAT_MS to show the code is running, and on
 * each step.
 */
static void led_task(void *params)
{
    (void)params;
    bool led_on = false;        // what the LED should show
    bool shown = false;         // what the LED is actually showing

#ifdef CYW43_WL_GPIO_LED_PIN
    hard_assert(cyw43_arch_init() == PICO_OK);              // FreeRTOS cyw43 must start inside a task
#else
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    TickType_t next_beat = xTaskGetTickCount() + pdMS_TO_TICKS(LED_HEARTBEAT_MS);

    while (1) {
        // Wake for a step, or every LED_POLL_MS to check the heartbeat
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_POLL_MS)) != 0) {
            led_on = !led_on;
        }
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - next_beat) >= 0) {
            next_beat += pdMS_TO_TICKS(LED_HEARTBEAT_MS);
            led_on = !led_on;
        }

        if (led_on != shown) {                              // only talk to the CYW43 on a change
            shown = led_on;
#ifdef CYW43_WL_GPIO_LED_PIN
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, shown);
#else
            gpio_put(PICO_DEFAULT_LED_PIN, shown);
#endif
        }
    }
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
    stepper_set_direction(1);

    move_queue = xQueueCreate(4, sizeof(move_t));
    configASSERT(move_queue != NULL);

    // LED shows each step
    xTaskCreate(led_task, "led", LED_TASK_STACK, NULL, LED_TASK_PRIORITY, &led_task_handle);
}

// SAA1042 CW/CCW: low = clockwise, high = counter-clockwise
void stepper_set_direction(uint8_t clockwise)
{
    gpio_put(STEPPER_DIR_PIN, !clockwise);
}

// Replaces whatever is running or queued with a new move
void stepper_move(int32_t pulses)
{
    move_t m = { .pulses = pulses, .gen = ++cmd_gen };
    xQueueReset(move_queue);
    xQueueSend(move_queue, &m, 0);
}

// Stops after the current pulse and drops anything queued
void stepper_stop(void)
{
    cmd_gen++;
    xQueueReset(move_queue);
}

void stepper_set_period_ms(uint32_t ms)
{
    if (ms < STEP_PERIOD_MS_MIN) {
        ms = STEP_PERIOD_MS_MIN;
    } else if (ms > STEP_PERIOD_MS_MAX) {
        ms = STEP_PERIOD_MS_MAX;
    }
    step_period_ms = ms;
}

void stepper_set_auto(bool on)
{
    auto_turn = on;
}

bool stepper_get_auto(void)
{
    return auto_turn;
}

void stepper_print_status(void)
{
    int32_t pos = position;
    printf("status: %s%s, position %ld pulses (%ld deg), period %lu ms (%lu ms/rev), %d pulses/rev, auto-turn %s\n",
           moving ? "moving " : "idle", moving ? (moving_cw ? "CW" : "CCW") : "",
           (long)pos, (long)(pos * 360 / PULSES_PER_REV),
           (unsigned long)step_period_ms, (unsigned long)(step_period_ms * PULSES_PER_REV),
           PULSES_PER_REV, auto_turn ? "on" : "off");
}

/* Pulses the clock until the move is done or replaced:
 * high for STEP_PULSE_WIDTH_MS, every step_period_ms.
 */
static void run_move(const move_t *m)
{
    bool cw = m->pulses > 0;
    bool continuous = m->pulses == STEPPER_CONTINUOUS || m->pulses == -STEPPER_CONTINUOUS;
    uint32_t total = continuous ? 0 : (uint32_t)(cw ? m->pulses : -m->pulses);
    uint32_t done = 0;

    // Set direction while the clock is low, before the first rising edge
    stepper_set_direction(cw);
    busy_wait_us(100);
    moving_cw = cw;
    moving = true;

    TickType_t last_wake = xTaskGetTickCount();
    while (m->gen == cmd_gen && (continuous || done < total)) {
        uint32_t period = step_period_ms;
        uint32_t width = STEP_PULSE_WIDTH_MS < period / 2 ? STEP_PULSE_WIDTH_MS : period / 2;

        gpio_put(STEPPER_CLOCK_PIN, 1);                     // Clock pin high
        xTaskNotifyGive(led_task_handle);                   // toggle LED each step
        vTaskDelay(pdMS_TO_TICKS(width));
        gpio_put(STEPPER_CLOCK_PIN, 0);                     // Clock pin low
        position += cw ? 1 : -1;
        done++;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(period));
    }

    moving = false;
    printf("move %s: %lu pulses %s (%lu deg), position %ld\n",
           m->gen == cmd_gen ? "done" : "stopped", (unsigned long)done, cw ? "CW" : "CCW",
           (unsigned long)(done * 360 / PULSES_PER_REV), (long)position);
}

/* Runs moves from the console. With auto-turn on it also turns
 * AUTO_TURN_DEGREES every AUTO_TURN_PERIOD_MS, waiting for console commands
 * in between.
 */
void stepper_task(void *params)
{
    (void)params;
    TickType_t next_turn = xTaskGetTickCount();
    move_t m;

    while (1) {
        TickType_t wait = pdMS_TO_TICKS(50);
        if (auto_turn) {
            int32_t until = (int32_t)(next_turn - xTaskGetTickCount());
            wait = until > 0 ? (TickType_t)until : 0;
        }

        if (xQueueReceive(move_queue, &m, wait) == pdTRUE) {
            run_move(&m);
        } else if (auto_turn) {
            m.pulses = AUTO_TURN_CW ? AUTO_TURN_PULSES : -AUTO_TURN_PULSES;
            m.gen = cmd_gen;
            run_move(&m);
            next_turn += pdMS_TO_TICKS(AUTO_TURN_PERIOD_MS);
            // A slow step period can make a turn outlast the interval: start the next one right away
            if ((int32_t)(xTaskGetTickCount() - next_turn) > 0) {
                next_turn = xTaskGetTickCount();
            }
        } else {
            next_turn = xTaskGetTickCount();    // turning auto-turn back on starts a turn right away
        }
    }
}
