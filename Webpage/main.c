// Raspberry Pi Pico W: Wi-Fi access point + DHCP server + tiny web server.
//
// Connect to SSID below, then browse to http://192.168.4.1/
// The page lets you toggle the Pico W's onboard LED.
 
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "dhcp_server.h"
 
#define AP_SSID      "PicoW_AP"
#define AP_PASSWORD  "picopico123"   // must be at least 8 characters
#define HTTP_PORT    80
 
static bool s_led_on = false;
 
static void set_led(bool on) {
    s_led_on = on;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}
 
static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;
    if (!p) {                       // client closed connection
        tcp_close(pcb);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
 
    char req[64] = {0};
    pbuf_copy_partial(p, req, sizeof(req) - 1, 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
 
    if (strncmp(req, "GET /led/on", 11) == 0)       set_led(true);
    else if (strncmp(req, "GET /led/off", 12) == 0) set_led(false);
 
    char body[512];
    // Change here if you want to change the HTML page that is served.
    int blen = snprintf(body, sizeof(body),
        "<!DOCTYPE html><html><head><meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\"><title>Pico W</title></head>"
        "<body style=\"font-family:sans-serif;text-align:center;margin-top:3em\">"
        "<h1>Pico W Access Point</h1>"
        "<p>LED is <b>%s</b></p>"
        "<p><a href=\"/led/on\">Turn ON</a> | <a href=\"/led/off\">Turn OFF</a></p>"
        "</body></html>",
        s_led_on ? "ON" : "OFF");
 
    char hdr[160];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n", blen);
 
    tcp_write(pcb, hdr, hlen, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    tcp_write(pcb, body, blen, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    tcp_close(pcb);                 // queued data is still sent before FIN
    return ERR_OK;
}
 
static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}
 
static bool http_server_start(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) return false;
    pcb = tcp_listen_with_backlog(pcb, 4);
    if (!pcb) return false;
    tcp_accept(pcb, http_accept);
    return true;
}
 
int main(void) {
    stdio_init_all();
 
    // This initializes the NIC of the CYW43 chip and allows the pico to work with its wifi capabilites.
    if (cyw43_arch_init()) {
        printf("cyw43 init failed\n");
        return 1;
    }
    // This function call comes from the SDK and enables the AP mode of the CYw43 Chip.
    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
 
    // Static address for the AP interface
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw,   192, 168, 4, 1);
 

    // The pico has two cores so in order to stop race conditions and other issues the lwip begin and lwip end functions
    // act as a lock to ensure that the lwip stack is not being used by both cores at the same time.
    cyw43_arch_lwip_begin();
    struct netif *ap = &cyw43_state.netif[CYW43_ITF_AP];
    netif_set_addr(ap, &ip, &mask, &gw);
    dhcp_server_start(ap);
    bool ok = http_server_start();
    cyw43_arch_lwip_end();
 
    printf("AP \"%s\" up, http %s at 192.168.4.1\n", AP_SSID, ok ? "ready" : "FAILED");
 
    set_led(true);
 
    while (true) {
        // All networking runs in the background (threadsafe_background mode).
        sleep_ms(1000);
    }
}
