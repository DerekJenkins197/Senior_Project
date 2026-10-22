#ifndef __RFID_H
#define __RFID_H

#include <stdint.h>

/* 125 kHz RFID reader module, UART output only. Same reader and logic as the
 * STM32 Digital-Padlock (USART1 RX on PA10).
 *   Module TX  -> 2k series resistor -> GP9 (pin 12, UART1 RX)
 *   Module GND -> GND, module +5V -> VBUS/+5V
 * The module's TX swings to 5 V and RP2040 GPIOs are 3.3 V only, hence the
 * series resistor (as on the STM32).
 *
 * Each read is sent as: 0x02, the tag as ASCII hex chars, 0x03.
 */
#define RFID_UART           uart1
#define RFID_RX_PIN         9
#define RFID_BAUD           9600

/* A tag is valid if its string contains this text (case matters, the reader
 * sends upper case). "BEAD1010" matches the EM410x card BE AD 10 10 00.
 * Every read is printed on the USB serial port. Override at configure time
 * with: cmake -DRFID_VALID_TAG=BEAD1010
 */
#ifndef RFID_VALID_TAG
#define RFID_VALID_TAG      "BEAD1010"
#endif

#define RFID_BUFFER_SIZE    16

/* The reader resends the frame every ~100 ms while the card is held on the
 * antenna. The same tag is only accepted again after it has been gone for
 * this long, so one tap = one trigger.
 */
#define RFID_REPEAT_MS      1000
#define RFID_DEBUG_STREAM   1       // print every UART byte received, and the GP9 line level, on USB serial
#define RFID_LINE_CHECK_MS  2000
#define RFID_POLL_MS        5       // UART FIFO is 32 bytes: ~33 ms of data at 9600 baud

#define RFID_TASK_PRIORITY  (tskIDLE_PRIORITY + 1)
#define RFID_TASK_STACK     (configMINIMAL_STACK_SIZE * 2)

void rfid_init(void);
void rfid_task(void *params);

#endif /* __RFID_H */
