// Raspberry Pi Pico W: Wi-Fi access point + DHCP server + web server + RFID-controlled stepper.
//
// Connect to the SSID below, then browse to http://192.168.4.1/
// On the page you can change the 5 accepted RFID codes and see the last 20 reads.
// A read of any accepted code runs the stepper for as long as the tag is held on the antenna.
 
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "dhcp_server.h"
#include "web.h"
#include "storage.h"
#include "rfid.h"
#include "stepper.h"
 
#define AP_SSID      "PicoW_AP"
#define AP_PASSWORD  "picopico123"   // must be at least 8 characters
 
int main(void) {
    stdio_init_all();       // USB serial only (see CMakeLists.txt); UART0 is left for the RFID reader
 
    store_init();           // load codes + log from flash (needs nothing else running)
    stepper_init();         // GP22 pulses the SAA1042 clock, GP21 sets direction
    rfid_init();            // GP1 = UART0 RX from the RFID module's TX
 
    // This initializes the CYW43 wireless chip.
    if (cyw43_arch_init()) {
        printf("cyw43 init failed\n");
        return 1;
    }
    // Enables the AP mode of the CYW43 chip.
    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
 
    // Static address for the AP interface
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw,   192, 168, 4, 1);
 
    // lwIP runs in the background, so anything touching it (or data shared with
    // its callbacks) is wrapped in lwip_begin/lwip_end.
    cyw43_arch_lwip_begin();
    struct netif *ap = &cyw43_state.netif[CYW43_ITF_AP];
    netif_set_addr(ap, &ip, &mask, &gw);
    dhcp_server_start(ap);
    bool ok = web_server_start();
    cyw43_arch_lwip_end();
 
    printf("AP \"%s\" up, http %s at 192.168.4.1\n", AP_SSID, ok ? "ready" : "FAILED");
 
    web_led_set(true);
 
    while (true) {
        rfid_poll();        // UART bytes -> frames -> accept/reject + log
        stepper_poll();     // step pulses
        store_poll();       // writes codes/log to flash shortly after they change
        sleep_ms(1);
    }
}
 