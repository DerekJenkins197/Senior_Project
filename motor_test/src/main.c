#include "FreeRTOS.h"
#include "task.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "stepper.h"

/* Debug console. Commands are typed on the Pico's USB serial port; output
 * also goes to a USB-UART adapter (PuTTY 115200 8N1, no flow control):
 *   GP4 (pin 6, UART1 TX) -> adapter RX
 *   GND (pin 8)           -> adapter GND
 */
#define CONSOLE_UART        uart1
#define CONSOLE_TX_PIN      4
#define CONSOLE_BAUD        115200

#define CONSOLE_TASK_PRIORITY   (tskIDLE_PRIORITY + 1)
#define CONSOLE_TASK_STACK      (configMINIMAL_STACK_SIZE * 2)
#define CONSOLE_POLL_MS         10
#define CONSOLE_LINE_MAX        32
#define MOVE_PULSES_MAX         100000

static void print_help(void)
{
    printf("\nmotor_test: SAA1042 clock GP%d, CW/CCW GP%d, %d pulses per output rev (1:%d gearbox)\n",
           STEPPER_CLOCK_PIN, STEPPER_DIR_PIN, PULSES_PER_REV, GEAR_RATIO);
    printf("  f [N]   N pulses CW  (default %d = 1 rev)\n", PULSES_PER_REV);
    printf("  b [N]   N pulses CCW (default %d = 1 rev)\n", PULSES_PER_REV);
    printf("  r / R   run CW / CCW until s\n");
    printf("  s       stop (also turns auto-turn off)\n");
    printf("  a       toggle auto-turn (%d deg %s every %d ms)\n",
           AUTO_TURN_DEGREES, AUTO_TURN_CW ? "CW" : "CCW", AUTO_TURN_PERIOD_MS);
    printf("  p MS    step period in ms (%d-%d), smaller = faster\n", STEP_PERIOD_MS_MIN, STEP_PERIOD_MS_MAX);
    printf("  ?       this help + status\n");
    stepper_print_status();
}

// Pulse count from the command's argument, or one revolution if there isn't one
static int32_t parse_pulses(const char *arg)
{
    char *end;
    long n = strtol(arg, &end, 10);
    if (end == arg) {
        return PULSES_PER_REV;
    }
    if (n < 1) {
        n = 1;
    } else if (n > MOVE_PULSES_MAX) {
        n = MOVE_PULSES_MAX;
    }
    return (int32_t)n;
}

static void run_command(char *line)
{
    while (isspace((unsigned char)*line)) {
        line++;
    }
    char cmd = *line;
    char *arg = cmd ? line + 1 : line;

    switch (cmd) {
    case '\0':
        break;
    case 'f':
        stepper_set_auto(false);
        stepper_move(parse_pulses(arg));
        break;
    case 'b':
        stepper_set_auto(false);
        stepper_move(-parse_pulses(arg));
        break;
    case 'r':
        stepper_set_auto(false);
        stepper_move(STEPPER_CONTINUOUS);
        break;
    case 'R':
        stepper_set_auto(false);
        stepper_move(-STEPPER_CONTINUOUS);
        break;
    case 's':
        stepper_set_auto(false);
        stepper_stop();
        break;
    case 'a':
        stepper_set_auto(!stepper_get_auto());
        if (!stepper_get_auto()) {
            stepper_stop();
        }
        printf("auto-turn %s\n", stepper_get_auto() ? "on" : "off");
        break;
    case 'p': {
        char *end;
        long ms = strtol(arg, &end, 10);
        if (end == arg || ms < 0) {
            printf("usage: p MS\n");
        } else {
            stepper_set_period_ms((uint32_t)ms);
            stepper_print_status();
        }
        break;
    }
    case '?':
    case 'h':
        print_help();
        break;
    default:
        printf("unknown command '%c', type ? for help\n", cmd);
        break;
    }
}

// Reads a line at a time from USB serial and runs it; prints the help when a terminal connects
static void console_task(void *params)
{
    (void)params;
    char line[CONSOLE_LINE_MAX];
    int len = 0;
    bool was_connected = false;

    while (1) {
        bool connected = stdio_usb_connected();
        if (connected && !was_connected) {
            print_help();
        }
        was_connected = connected;

        int c;
        while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                if (len > 0) {
                    printf("\n");
                    line[len] = '\0';
                    len = 0;
                    run_command(line);
                }
            } else if ((c == '\b' || c == 0x7f) && len > 0) {
                len--;
                printf("\b \b");
            } else if (isprint(c) && len < CONSOLE_LINE_MAX - 1) {
                line[len++] = (char)c;
                putchar(c);                                 // echo, PuTTY doesn't by default
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_POLL_MS));
    }
}

int main(void)
{
    // Not stdio_init_all(): keep the console off the default UART0 pins (GP0/GP1)
    stdio_usb_init();
    stdio_uart_init_full(CONSOLE_UART, CONSOLE_BAUD, CONSOLE_TX_PIN, -1);   // TX only
    printf("\nmotor_test starting, type ? for help\n");

    stepper_init();         // GP21 pulses the SAA1042 clock, GP20 sets direction

    xTaskCreate(console_task, "console", CONSOLE_TASK_STACK, NULL,
                CONSOLE_TASK_PRIORITY, NULL);
    xTaskCreate(stepper_task, "stepper", STEPPER_TASK_STACK, NULL,
                STEPPER_TASK_PRIORITY, NULL);

    vTaskStartScheduler();

    // Only reached if there wasn't enough heap to start the scheduler
    while (1) {
    }
    return -1;
}
