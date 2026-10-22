#include "FreeRTOS.h"
#include "task.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "rfid.h"
#include "stepper.h"

#define RFID_STX    0x02
#define RFID_ETX    0x03
#define RFID_RAW_MAX    32

void rfid_init(void)
{
    // RX only: the module has no input, so GP8 (UART1 TX) stays free
    uart_init(RFID_UART, RFID_BAUD);
    uart_set_format(RFID_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(RFID_UART, true);
    gpio_set_function(RFID_RX_PIN, GPIO_FUNC_UART);
#if RFID_DEBUG_STREAM
    gpio_pull_down(RFID_RX_PIN);            // line only reads high if the module's TX is driving it
#else
    gpio_pull_up(RFID_RX_PIN);              // hold the line idle (high) if the module is unplugged
#endif
}

#if RFID_DEBUG_STREAM
// "RFID rx: 02 42 45 ... |.BE...|": every UART byte, in hex and printable ASCII
static void print_raw(const uint8_t *bytes, int len)
{
    printf("RFID rx:");
    for (int i = 0; i < len; i++) {
        printf(" %02X", bytes[i]);
    }
    printf("  |");
    for (int i = 0; i < len; i++) {
        putchar(isprint(bytes[i]) ? bytes[i] : '.');
    }
    printf("|\n");
}
#endif

// Same check as the STM32 RFID_IsValid(): the tag string contains RFID_VALID_TAG
static bool rfid_is_valid(const char *tag)
{
    return strstr(tag, RFID_VALID_TAG) != NULL;
}

/* Same framing as the STM32 USART1_IRQHandler: 0x02 starts a new tag, 0x03
 * ends it, everything in between is buffered. A finished tag triggers the
 * stepper if it is valid. With RFID_DEBUG_STREAM every received byte is also
 * printed, and the LED lights on any received byte.
 */
void rfid_task(void *params)
{
    (void)params;
    char rfid_buffer[RFID_BUFFER_SIZE];
    uint8_t rfid_index = 0;

    char last_tag[RFID_BUFFER_SIZE] = "";
    TickType_t last_seen = 0;

#if RFID_DEBUG_STREAM
    uint8_t raw[RFID_RAW_MAX];
    int raw_len = 0;
    TickType_t next_line_check = xTaskGetTickCount();
#endif

    while (1) {
        while (uart_is_readable(RFID_UART)) {
            char c = uart_getc(RFID_UART);
            led_rx_activity();

#if RFID_DEBUG_STREAM
            if (raw_len == RFID_RAW_MAX) {
                print_raw(raw, raw_len);
                raw_len = 0;
            }
            raw[raw_len++] = (uint8_t)c;
#endif

            if (c == RFID_STX) {
                rfid_index = 0;
            } else if (c == RFID_ETX) {
                rfid_buffer[rfid_index] = '\0';
                rfid_index = 0;
                printf("RFID: Raw Tag Read: [%s]\n", rfid_buffer);

                // Same card still on the antenna: refresh the timer, don't retrigger
                TickType_t now = xTaskGetTickCount();
                bool repeat = strcmp(rfid_buffer, last_tag) == 0 &&
                              (now - last_seen) < pdMS_TO_TICKS(RFID_REPEAT_MS);
                strcpy(last_tag, rfid_buffer);
                last_seen = now;
                if (repeat) {
                    continue;
                }

                if (rfid_is_valid(rfid_buffer)) {
                    printf("RFID: -> AUTH SUCCESS\n");
                    stepper_trigger();
                } else {
                    printf("RFID: -> AUTH FAILED\n");
                }
            } else if (rfid_index < RFID_BUFFER_SIZE - 1) {
                rfid_buffer[rfid_index++] = c;
            }
        }

#if RFID_DEBUG_STREAM
        // Line went quiet: show the burst as one line
        if (raw_len > 0) {
            print_raw(raw, raw_len);
            raw_len = 0;
        }

        // An idle UART line sits high, so with the pull-down a low line means nothing is driving GP9
        if ((int32_t)(xTaskGetTickCount() - next_line_check) >= 0) {
            next_line_check += pdMS_TO_TICKS(RFID_LINE_CHECK_MS);
            printf(gpio_get(RFID_RX_PIN) ? "RFID: GP%d high, reader TX connected\n"
                                         : "RFID: GP%d low, nothing driving it (check reader TX wire/power)\n",
                   RFID_RX_PIN);
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(RFID_POLL_MS));
    }
}
