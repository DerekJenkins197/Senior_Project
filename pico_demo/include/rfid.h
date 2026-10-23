#ifndef __RFID_H
#define __RFID_H

#include <stdint.h>

/* RDM6300 125 kHz RFID reader, UART output only (9600 8N1).
 *   Module TX  -> 2k series resistor -> GP1 (pin 2, UART0 RX)
 *   Module GND -> GND, module +5V -> VBUS/+5V
 * The module's TX swings to 5 V and RP2040 GPIOs are 3.3 V only, hence the
 * series resistor (as on the STM32).
 *
 * Each read is 14 bytes: 0x02, 10 ASCII hex data chars, 2 ASCII hex
 * checksum chars (XOR of the 5 data bytes), 0x03.
 */
#define RFID_UART           uart0
#define RFID_RX_PIN         1
#define RFID_BAUD           9600

/* Self-test: sends fake RDM6300 frames out of UART0 TX, alternating a valid
 * and an invalid tag every RFID_TEST_PERIOD_MS, so the whole receive path can
 * be checked without a card.
 *   0 = off, normal operation
 *   1 = internal loopback: UART0 TX is wired to RX inside the chip, no
 *       wiring needed (GP1 is ignored while this is on)
 *   2 = external: frames go out on GP0 (pin 1). Disconnect the reader's TX
 *       and jumper GP0 -> GP1 to also test the GP1 pin and wire
 * Override at configure time with: cmake -DRFID_SELFTEST=1
 */
#ifndef RFID_SELFTEST
#define RFID_SELFTEST       0
#endif
#define RFID_TEST_TX_PIN    0
#define RFID_TEST_PERIOD_MS 2000

/* The 10 data chars a valid tag must send, exactly (upper case, as the
 * reader sends it). "BEAD101000" is the EM410x card BE AD 10 10 00.
 * Every read is printed on the console. Override at configure time
 * with: cmake -DRFID_VALID_TAG=BEAD101000
 */
#ifndef RFID_VALID_TAG
#define RFID_VALID_TAG      "BEAD101000"
#endif

#define RFID_BUFFER_SIZE    16

/* The reader resends the frame every ~100 ms while the card is held on the
 * antenna. The motor keeps stepping while the valid tag keeps arriving, and
 * stops once no valid frame has come in for this long (or another tag is read).
 */
#define RFID_GONE_MS        500
#define RFID_DEBUG_STREAM   1       // print every UART byte received, and the GP1 line level, on the console
#define RFID_LINE_CHECK_MS  2000
#define RFID_RX_BUFFER      256     // bytes the UART interrupt can queue for the task

#define RFID_TASK_PRIORITY  (tskIDLE_PRIORITY + 1)
#define RFID_TASK_STACK     (configMINIMAL_STACK_SIZE * 2)

void rfid_init(void);
void rfid_task(void *params);
void rfid_test_task(void *params);  // only built with RFID_SELFTEST

#endif /* __RFID_H */
