#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/uart.h"
#include "rfid.h"
#include "stepper.h"
#include "storage.h"
 
#define RFID_STX 0x02
#define RFID_ETX 0x03
 
static char     s_frame[RFID_BUFFER_SIZE];
static uint8_t  s_idx;
static bool     s_in_frame;
 
static bool     s_tag_present;                  // any tag with a good checksum on the antenna
static char     s_cur_tag[RFID_DATA_LEN + 1];
static uint32_t s_last_frame_ms;
 
static bool     s_valid_present;                // an accepted tag is on the antenna, stepper running
static uint32_t s_last_valid_ms;
 
static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
 
void rfid_init(void)
{
    uart_init(RFID_UART, RFID_BAUD);
    uart_set_format(RFID_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(RFID_UART, false, false);
    uart_set_fifo_enabled(RFID_UART, true);
    gpio_set_function(RFID_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(RFID_RX_PIN);      // line idles high if the module is unplugged
    // RX only: the module has no input, so GP0 (UART0 TX) stays free
}
 
// Two ASCII hex chars -> byte, or -1 if either isn't hex
static int hex_byte(const char *s)
{
    if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) return -1;
    char pair[3] = { s[0], s[1], '\0' };
    return (int)strtol(pair, NULL, 16);
}
 
// RDM6300 checksum: XOR of the 5 data bytes equals the last byte
static bool checksum_ok(const char *frame)
{
    if (strlen(frame) != RFID_FRAME_LEN) return false;
    int sum = 0;
    for (int i = 0; i < RFID_FRAME_LEN; i += 2) {
        int b = hex_byte(&frame[i]);
        if (b < 0) return false;
        sum ^= b;                   // XOR over data and checksum is 0 when they match
    }
    return sum == 0;
}
 
static void handle_frame(const char *frame, uint32_t now)
{
    if (!checksum_ok(frame)) {
        printf("RFID: bad frame [%s]\n", frame);
        return;
    }
 
    char tag[RFID_DATA_LEN + 1];
    memcpy(tag, frame, RFID_DATA_LEN);
    tag[RFID_DATA_LEN] = '\0';
 
    // A "read" = a tag newly arriving, or a different tag. The repeats while it is held don't count.
    bool is_new = !s_tag_present || strcmp(tag, s_cur_tag) != 0;
 
    cyw43_arch_lwip_begin();            // codes and log are shared with the web server
    int slot = store_match(tag);
    if (is_new) store_log_add(tag, slot);
    cyw43_arch_lwip_end();
 
    if (is_new) {
        printf("RFID: tag %s -> %s", tag, slot ? "ACCEPTED" : "REJECTED");
        if (slot) printf(" (code %d)", slot);
        printf("\n");
    }
 
    strcpy(s_cur_tag, tag);
    s_tag_present = true;
    s_last_frame_ms = now;
 
    if (slot) {
        s_last_valid_ms = now;
        if (!s_valid_present) {
            s_valid_present = true;
            stepper_start();
        }
    } else if (s_valid_present) {       // a different, unaccepted tag replaced the good one
        s_valid_present = false;
        stepper_stop();
    }
}
 
void rfid_poll(void)
{
    while (uart_is_readable(RFID_UART)) {
        uint32_t dr = uart_get_hw(RFID_UART)->dr;       // data in bits 7:0, error flags above
        if (dr & UART_UARTDR_BE_BITS) continue;          // a break is not a real byte
        uint8_t c = (uint8_t)dr;
 
        if (c == RFID_STX) {
            s_idx = 0;
            s_in_frame = true;
        } else if (c == RFID_ETX) {
            if (s_in_frame) {
                s_frame[s_idx] = '\0';
                handle_frame(s_frame, now_ms());
            }
            s_in_frame = false;
            s_idx = 0;
        } else if (s_in_frame && s_idx < RFID_BUFFER_SIZE - 1) {
            s_frame[s_idx++] = (char)c;
        }
    }
 
    uint32_t now = now_ms();
    if (s_tag_present && (int32_t)(now - s_last_frame_ms) >= (int32_t)RFID_GONE_MS) {
        s_tag_present = false;          // tag taken away; reading it again counts as a new read
    }
    if (s_valid_present && (int32_t)(now - s_last_valid_ms) >= (int32_t)RFID_GONE_MS) {
        s_valid_present = false;
        stepper_stop();
        printf("RFID: accepted tag gone, stopping\n");
    }
}
 