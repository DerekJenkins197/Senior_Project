#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "rfid.h"
#include "stepper.h"

#define RFID_STX        0x02
#define RFID_ETX        0x03
#define RFID_DATA_LEN   10      // ASCII hex chars of tag data in an RDM6300 frame
#define RFID_FRAME_LEN  12      // data + 2 checksum chars
#define RFID_RAW_MAX    32

static StreamBufferHandle_t rx_stream;

// Receive counters, updated by the UART interrupt and printed by the line check
static volatile uint32_t rx_bytes;
static volatile uint32_t rx_framing_errors;     // usually wrong baud, or a floating/noisy line
static volatile uint32_t rx_break_errors;       // line held low for a whole byte
static volatile uint32_t rx_overruns;           // bytes lost: hardware FIFO or stream buffer full

/* Moves every byte out of the 32-byte hardware FIFO into rx_stream, so slow
 * printf()s in any task can no longer make the FIFO overflow.
 */
static void rfid_uart_irq(void)
{
    BaseType_t woken = pdFALSE;
    uart_hw_t *hw = uart_get_hw(RFID_UART);

    while (uart_is_readable(RFID_UART)) {
        uint32_t dr = hw->dr;                   // data in bits 7:0, error flags above
        if (dr & UART_UARTDR_FE_BITS) {
            rx_framing_errors++;
        }
        if (dr & UART_UARTDR_BE_BITS) {
            rx_break_errors++;
            continue;                           // a break is not a real byte
        }
        if (dr & UART_UARTDR_OE_BITS) {
            rx_overruns++;
        }
        uint8_t c = (uint8_t)dr;
        rx_bytes++;
        if (xStreamBufferSendFromISR(rx_stream, &c, 1, &woken) != 1) {
            rx_overruns++;
        }
    }
    portYIELD_FROM_ISR(woken);
}

void rfid_init(void)
{
    rx_stream = xStreamBufferCreate(RFID_RX_BUFFER, 1);
    configASSERT(rx_stream != NULL);

    uart_init(RFID_UART, RFID_BAUD);
    uart_set_format(RFID_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(RFID_UART, false, false);
    uart_set_fifo_enabled(RFID_UART, true);
    gpio_set_function(RFID_RX_PIN, GPIO_FUNC_UART);
#if RFID_DEBUG_STREAM
    gpio_pull_down(RFID_RX_PIN);            // line only reads high if the module's TX is driving it
#else
    gpio_pull_up(RFID_RX_PIN);              // hold the line idle (high) if the module is unplugged
#endif

#if RFID_SELFTEST == 1
    hw_set_bits(&uart_get_hw(RFID_UART)->cr, UART_UARTCR_LBE_BITS);   // TX -> RX inside the UART
#elif RFID_SELFTEST == 2
    gpio_set_function(RFID_TEST_TX_PIN, GPIO_FUNC_UART);              // jumper GP0 -> GP1
#endif
    // Otherwise RX only: the module has no input, so GP0 (UART0 TX) stays free

    int irq = RFID_UART == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, rfid_uart_irq);
    irq_set_enabled(irq, true);
    uart_set_irq_enables(RFID_UART, true, false);
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

// The tag's 10 data chars (checksum not included) are exactly RFID_VALID_TAG
static bool rfid_is_valid(const char *tag)
{
    return strlen(tag) == RFID_FRAME_LEN && strncmp(tag, RFID_VALID_TAG, RFID_DATA_LEN) == 0;
}

// Two ASCII hex chars -> byte, or -1 if either isn't hex
static int hex_byte(const char *s)
{
    if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) {
        return -1;
    }
    char pair[3] = { s[0], s[1], '\0' };
    return (int)strtol(pair, NULL, 16);
}

// RDM6300 checksum: XOR of the 5 data bytes equals the last byte
static bool rfid_checksum_ok(const char *tag)
{
    if (strlen(tag) != RFID_FRAME_LEN) {
        return false;
    }
    int sum = 0;
    for (int i = 0; i < RFID_FRAME_LEN; i += 2) {
        int b = hex_byte(&tag[i]);
        if (b < 0) {
            return false;
        }
        sum ^= b;           // XOR over data and checksum is 0 when they match
    }
    return sum == 0;
}

/* Same framing as the STM32 USART1_IRQHandler: 0x02 starts a new tag, 0x03
 * ends it, everything in between is buffered. The first valid tag frame
 * starts the stepper; it keeps running while valid frames keep arriving and
 * stops when none has come in for RFID_GONE_MS, or a different tag is read.
 * With RFID_DEBUG_STREAM every received byte is also printed, and the LED
 * lights on any received byte.
 */
void rfid_task(void *params)
{
    (void)params;
    char rfid_buffer[RFID_BUFFER_SIZE];
    uint8_t rfid_index = 0;

    bool valid_present = false;     // valid tag on the antenna, stepper running
    TickType_t last_valid = 0;      // tick of the last valid frame

    printf("RFID: listening on GP%d, UART%d %d 8N1, valid tag \"%s\"\n",
           RFID_RX_PIN, uart_get_index(RFID_UART), RFID_BAUD, RFID_VALID_TAG);
#if RFID_SELFTEST == 1
    printf("RFID: SELF-TEST 1, internal loopback (GP1 ignored)\n");
#elif RFID_SELFTEST == 2
    printf("RFID: SELF-TEST 2, sending on GP%d, jumper it to GP%d\n", RFID_TEST_TX_PIN, RFID_RX_PIN);
#endif

#if RFID_DEBUG_STREAM
    uint8_t raw[RFID_RAW_MAX];
    int raw_len = 0;
    TickType_t next_line_check = xTaskGetTickCount();
#endif

    while (1) {
        // Valid tag stopped arriving: it has been taken off the antenna
        if (valid_present && (xTaskGetTickCount() - last_valid) >= pdMS_TO_TICKS(RFID_GONE_MS)) {
            valid_present = false;
            printf("RFID: valid tag gone, stopping\n");
            stepper_stop();
        }

        uint8_t c;
        // Bytes 1 ms apart at 9600 baud: 20 ms of silence ends a burst
        if (xStreamBufferReceive(rx_stream, &c, 1, pdMS_TO_TICKS(20)) == 1) {
            led_rx_activity();

#if RFID_DEBUG_STREAM
            if (raw_len == RFID_RAW_MAX) {
                print_raw(raw, raw_len);
                raw_len = 0;
            }
            raw[raw_len++] = c;
#endif

            if (c == RFID_STX) {
                rfid_index = 0;
            } else if (c == RFID_ETX) {
                rfid_buffer[rfid_index] = '\0';
                rfid_index = 0;
                bool sum_ok = rfid_checksum_ok(rfid_buffer);
                printf("RFID: Raw Tag Read: [%s]  checksum %s\n", rfid_buffer, sum_ok ? "OK" : "BAD");

                if (sum_ok && rfid_is_valid(rfid_buffer)) {
                    last_valid = xTaskGetTickCount();
                    if (!valid_present) {
                        valid_present = true;
                        printf("RFID: -> AUTH SUCCESS, stepping while the tag is present\n");
                        stepper_start();
                    }
                } else {
                    printf("RFID: -> AUTH FAILED\n");
                    if (valid_present) {
                        valid_present = false;
                        printf("RFID: other tag read, stopping\n");
                        stepper_stop();
                    }
                }
            } else if (rfid_index < RFID_BUFFER_SIZE - 1) {
                rfid_buffer[rfid_index++] = c;
            }
            continue;
        }

#if RFID_DEBUG_STREAM
        // Line went quiet: show the burst as one line
        if (raw_len > 0) {
            print_raw(raw, raw_len);
            raw_len = 0;
        }

        // An idle UART line sits high, so with the pull-down a low line means nothing is driving GP1
        if ((int32_t)(xTaskGetTickCount() - next_line_check) >= 0) {
            next_line_check += pdMS_TO_TICKS(RFID_LINE_CHECK_MS);
#if RFID_SELFTEST != 1
            printf(gpio_get(RFID_RX_PIN) ? "RFID: GP%d high, reader TX connected"
                                         : "RFID: GP%d low, nothing driving it (check reader TX wire/power)",
                   RFID_RX_PIN);
#else
            printf("RFID: loopback");
#endif
            printf("  | bytes %lu, framing errs %lu, breaks %lu, overruns %lu\n",
                   (unsigned long)rx_bytes, (unsigned long)rx_framing_errors,
                   (unsigned long)rx_break_errors, (unsigned long)rx_overruns);
        }
#endif
    }
}

#if RFID_SELFTEST
/* Plays the part of the RDM6300: sends a full frame (0x02, data, checksum,
 * 0x03) out of UART0 TX, alternating the valid card BE AD 10 10 00 and an
 * invalid one, so the log should show AUTH SUCCESS / AUTH FAILED in turn.
 */
void rfid_test_task(void *params)
{
    (void)params;
    static const char *const frames[] = {
        "\x02" "BEAD10100013" "\x03",   // valid: BE^AD^10^10^00 = 13
        "\x02" "001122334444" "\x03",   // invalid tag, good checksum
    };
    unsigned int n = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(RFID_TEST_PERIOD_MS));
        const char *frame = frames[n++ % 2];
        printf("TEST: sending fake tag %.12s\n", frame + 1);
        uart_write_blocking(RFID_UART, (const uint8_t *)frame, strlen(frame));
    }
}
#endif
