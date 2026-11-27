#ifndef __RFID_H
#define __RFID_H
 
#include <stdint.h>
 
/* RDM6300 125 kHz RFID reader, UART output only (9600 8N1).
 *   Module TX  -> 2k series resistor -> GP1 (pin 2, UART0 RX)
 *   Module GND -> GND, module +5V -> VBUS/+5V
 * The module's TX swings to 5 V and RP2040 GPIOs are 3.3 V only, hence the
 * series resistor.
 *
 * Each read is 14 bytes: 0x02, 10 ASCII hex data chars, 2 ASCII hex
 * checksum chars (XOR of the 5 data bytes), 0x03.
 *
 * Which tags are accepted is configured on the web page (5 codes, see storage.h).
 * USB serial is the debug console (UART0 belongs to the reader).
 */
#define RFID_UART           uart0
#define RFID_RX_PIN         1
#define RFID_BAUD           9600
 
#define RFID_DATA_LEN       10      // ASCII hex chars of tag data in a frame
#define RFID_FRAME_LEN      12      // data + 2 checksum chars
#define RFID_BUFFER_SIZE    16
 
/* The reader resends the frame every ~100 ms while the card is held on the
 * antenna. The motor keeps stepping while an accepted tag keeps arriving, and
 * stops once none has come in for this long (or a different tag is read).
 */
#define RFID_GONE_MS        500
 
void rfid_init(void);
void rfid_poll(void);               // call often (every few ms) from the main loop
 
#endif /* __RFID_H */
 