#ifndef WEB_H
#define WEB_H
 
#include <stdbool.h>
 
/* Tiny HTTP server on port 80. Call with the lwIP lock held.
 *   GET /            the page (edit the 5 codes, see the last 20 reads, toggle the LED)
 *   GET /api/state   JSON: codes + log
 *   GET /set?slot=1..5&code=XXXXXXXXXX
 *   GET /led/on, /led/off
 */
bool web_server_start(void);
void web_led_set(bool on);
 
#endif
 