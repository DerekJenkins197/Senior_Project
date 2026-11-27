#ifndef STORAGE_H
#define STORAGE_H
 
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
 
/* Persistent settings + read log, kept in the last 4 KB sector of flash.
 *
 * Thread rules (lwIP runs in the background, see main.c):
 *   - lwIP callbacks (the web server) already hold the lwIP lock, so they
 *     may call everything here directly.
 *   - Code running in the main loop must wrap calls in
 *     cyw43_arch_lwip_begin() / cyw43_arch_lwip_end().
 *     (store_poll() does its own locking.)
 */
 
#define STORE_NUM_CODES     5
#define STORE_CODE_LEN      10      /* ASCII hex chars of an RDM6300 tag */
#define STORE_LOG_MAX       20
#define STORE_DEFAULT_CODE  "BEAD101000"
 
typedef struct {
    char     tag[STORE_CODE_LEN + 1];
    uint8_t  authorized;            /* 1 = matched one of the codes */
    uint8_t  slot;                  /* which code matched, 1..5 (0 = none) */
    uint8_t  _pad;
    uint32_t uptime_s;              /* seconds since boot when it was read */
    uint32_t boot;                  /* boot counter, so reads can be ordered across resets */
} store_log_entry_t;
 
typedef struct {
    uint32_t magic;
    uint32_t boot_count;
    uint8_t  log_count;             /* 0..STORE_LOG_MAX */
    char     codes[STORE_NUM_CODES][STORE_CODE_LEN + 1];
    store_log_entry_t log[STORE_LOG_MAX];   /* log[0] = most recent */
    uint32_t crc;
} store_data_t;
 
void store_init(void);                              /* load from flash (or defaults) */
void store_poll(void);                              /* call from the main loop: writes flash when something changed */
const store_data_t *store_get(void);                /* read-only view of the data */
bool store_set_code(int slot0, const char *code);   /* slot0 = 0..4; code = 10 hex chars */
int  store_match(const char *tag10);                /* 1..5 = matching code slot, 0 = no match */
void store_log_add(const char *tag10, int slot);    /* newest first, capped at STORE_LOG_MAX */
 
#endif /* STORAGE_H */
 