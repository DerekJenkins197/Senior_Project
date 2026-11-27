#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "storage.h"
#include "web.h"
 
#define HTTP_PORT 80
 
static bool s_led_on = false;
 
void web_led_set(bool on)
{
    s_led_on = on;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}
 
// The whole page lives in flash and is sent without copying. Change the HTML/JS here.
// (Single quotes only, so no escaping is needed inside the C strings.)
static const char PAGE[] =
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Pico RFID</title><style>"
"body{font-family:sans-serif;max-width:640px;margin:1em auto;padding:0 1em}"
"input{font-family:monospace;text-transform:uppercase}"
"table{border-collapse:collapse;width:100%}"
"td,th{border:1px solid #ccc;padding:4px;text-align:left;font-size:14px}"
".ok{color:#080}.no{color:#a00}"
"</style></head><body>"
"<h1>Pico W RFID</h1>"
"<h2>Accepted codes</h2>"
"<p>10 hex characters (0-9, A-F), e.g. BEAD101000.</p>"
"<div id='codes'>Loading...</div><p id='msg'></p>"
"<h2>Last 20 reads</h2>"
"<table><thead><tr><th>#</th><th>Tag</th><th>Result</th><th>When</th></tr></thead>"
"<tbody id='log'></tbody></table>"
"<p>LED: <button onclick='led(1)'>On</button> <button onclick='led(0)'>Off</button></p>"
"<script>"
"var first=true;"
"function load(){fetch('/api/state').then(function(r){return r.json();}).then(function(s){"
"if(first){first=false;var h='';"
"for(var i=0;i<s.codes.length;i++){"
"h+='<p>Code '+(i+1)+': <input id=c'+i+' maxlength=10 size=12 value='+s.codes[i]+'> "
"<button onclick=save('+i+')>Save</button></p>';}"
"document.getElementById('codes').innerHTML=h;}"
"var t='';"
"for(var j=0;j<s.log.length;j++){var e=s.log[j];"
"t+='<tr><td>'+(j+1)+'</td><td>'+e.tag+'</td><td class='+(e.ok?'ok':'no')+'>'"
"+(e.ok?'Accepted (code '+e.slot+')':'Rejected')+'</td><td>boot '+e.boot+', +'+e.t+' s</td></tr>';}"
"document.getElementById('log').innerHTML=t||'<tr><td colspan=4>No reads yet</td></tr>';"
"});}"
"function save(i){var v=document.getElementById('c'+i).value.toUpperCase();"
"fetch('/set?slot='+(i+1)+'&code='+encodeURIComponent(v))"
".then(function(r){return r.text();})"
".then(function(t){document.getElementById('msg').textContent=t;});}"
"function led(on){fetch('/led/'+(on?'on':'off'));}"
"load();setInterval(load,3000);"
"</script></body></html>";
 
static void send_response(struct tcp_pcb *pcb, const char *status, const char *ctype,
                          const char *body, int blen, bool copy_body)
{
    char hdr[192];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, ctype, blen);
 
    tcp_write(pcb, hdr, hlen, TCP_WRITE_FLAG_COPY | (blen > 0 ? TCP_WRITE_FLAG_MORE : 0));
    if (blen > 0) {
        tcp_write(pcb, body, blen, copy_body ? TCP_WRITE_FLAG_COPY : 0);
    }
    tcp_output(pcb);
    tcp_close(pcb);                 // queued data is still sent before FIN
}
 
static void send_text(struct tcp_pcb *pcb, const char *status, const char *text)
{
    send_response(pcb, status, "text/plain", text, (int)strlen(text), true);
}
 
// "slot=2&code=ABC" + "code" -> "ABC"
static bool query_get(const char *q, const char *key, char *out, size_t outsz)
{
    size_t kl = strlen(key);
    while (q && *q) {
        if (strncmp(q, key, kl) == 0 && q[kl] == '=') {
            q += kl + 1;
            size_t i = 0;
            while (*q && *q != '&' && i < outsz - 1) out[i++] = *q++;
            out[i] = '\0';
            return true;
        }
        q = strchr(q, '&');
        if (q) q++;
    }
    return false;
}
 
static int build_json(char *buf, size_t n)
{
    const store_data_t *d = store_get();
    int len = 0;
#define APPEND(...) do { if (len < (int)n) len += snprintf(buf + len, n - (size_t)len, __VA_ARGS__); } while (0)
 
    APPEND("{\"boot\":%lu,\"led\":%d,\"codes\":[", (unsigned long)d->boot_count, s_led_on ? 1 : 0);
    for (int i = 0; i < STORE_NUM_CODES; i++) {
        APPEND("%s\"%s\"", i ? "," : "", d->codes[i]);
    }
    APPEND("],\"log\":[");
    for (int i = 0; i < d->log_count; i++) {
        const store_log_entry_t *e = &d->log[i];
        APPEND("%s{\"tag\":\"%s\",\"ok\":%d,\"slot\":%d,\"boot\":%lu,\"t\":%lu}",
               i ? "," : "", e->tag, e->authorized ? 1 : 0, e->slot,
               (unsigned long)e->boot, (unsigned long)e->uptime_s);
    }
    APPEND("]}");
#undef APPEND
    if (len >= (int)n) len = (int)n - 1;     // cannot happen with the buffer size used below
    return len;
}
 
static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (!p) {                       // client closed connection
        tcp_close(pcb);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
 
    char req[256] = {0};
    pbuf_copy_partial(p, req, sizeof(req) - 1, 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
 
    if (strncmp(req, "GET ", 4) != 0) {
        send_text(pcb, "405 Method Not Allowed", "GET only");
        return ERR_OK;
    }
 
    // Path (and optional query) from the request line
    char path[128];
    size_t i = 0;
    const char *s = req + 4;
    while (s[i] && s[i] != ' ' && s[i] != '\r' && s[i] != '\n' && i < sizeof(path) - 1) {
        path[i] = s[i];
        i++;
    }
    path[i] = '\0';
    char *query = strchr(path, '?');
    if (query) *query++ = '\0';
 
    if (strcmp(path, "/") == 0) {
        send_response(pcb, "200 OK", "text/html", PAGE, (int)sizeof(PAGE) - 1, false);
    } else if (strcmp(path, "/api/state") == 0) {
        static char json[2560];
        int jlen = build_json(json, sizeof(json));
        send_response(pcb, "200 OK", "application/json", json, jlen, true);
    } else if (strcmp(path, "/set") == 0) {
        char slot_s[8], code[24];
        if (!query || !query_get(query, "slot", slot_s, sizeof(slot_s)) ||
            !query_get(query, "code", code, sizeof(code))) {
            send_text(pcb, "400 Bad Request", "Error: need slot and code");
        } else {
            int slot = atoi(slot_s);
            if (slot >= 1 && slot <= STORE_NUM_CODES && store_set_code(slot - 1, code)) {
                char msg[48];
                snprintf(msg, sizeof(msg), "Saved code %d", slot);
                send_text(pcb, "200 OK", msg);
            } else {
                send_text(pcb, "400 Bad Request", "Error: code must be exactly 10 hex characters (0-9, A-F)");
            }
        }
    } else if (strcmp(path, "/led/on") == 0) {
        web_led_set(true);
        send_text(pcb, "200 OK", "OK");
    } else if (strcmp(path, "/led/off") == 0) {
        web_led_set(false);
        send_text(pcb, "200 OK", "OK");
    } else {
        send_text(pcb, "404 Not Found", "Not found");
    }
    return ERR_OK;
}
 
static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}
 
bool web_server_start(void)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) return false;
    pcb = tcp_listen_with_backlog(pcb, 4);
    if (!pcb) return false;
    tcp_accept(pcb, http_accept);
    return true;
}
 