#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "storage.h"
 
#define STORE_MAGIC         0x52464431u     /* "RFD1" */
#define STORE_FLASH_BYTES   1024            /* programmed size, multiple of FLASH_PAGE_SIZE (256) */
#define STORE_FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)   /* last sector */
#define FLUSH_DELAY_MS      200             /* batch changes that arrive close together */
 
_Static_assert(sizeof(store_data_t) <= STORE_FLASH_BYTES, "store_data_t does not fit the flash block");
 
static store_data_t g;
static bool     s_dirty;
static uint32_t s_dirty_since;
 
static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
 
static uint32_t fnv1a(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}
 
static uint32_t calc_crc(const store_data_t *d) { return fnv1a(d, offsetof(store_data_t, crc)); }
 
static void mark_dirty(void)
{
    if (!s_dirty) {
        s_dirty = true;
        s_dirty_since = now_ms();
    }
}
 
// 10 chars, each 0-9 or A-F
static bool code_ok(const char *c)
{
    for (int i = 0; i < STORE_CODE_LEN; i++) {
        if (!((c[i] >= '0' && c[i] <= '9') || (c[i] >= 'A' && c[i] <= 'F'))) return false;
    }
    return c[STORE_CODE_LEN] == '\0';
}
 
static bool plausible(const store_data_t *d)
{
    if (d->magic != STORE_MAGIC || d->crc != calc_crc(d) || d->log_count > STORE_LOG_MAX) return false;
    for (int i = 0; i < STORE_NUM_CODES; i++) {
        if (!code_ok(d->codes[i])) return false;
    }
    return true;
}
 
void store_init(void)
{
    const store_data_t *f = (const store_data_t *)(XIP_BASE + STORE_FLASH_OFFSET);
    if (plausible(f)) {
        memcpy(&g, f, sizeof g);
        printf("STORE: loaded from flash, %u log entries\n", g.log_count);
    } else {
        memset(&g, 0, sizeof g);
        g.magic = STORE_MAGIC;
        for (int i = 0; i < STORE_NUM_CODES; i++) {
            memcpy(g.codes[i], STORE_DEFAULT_CODE, STORE_CODE_LEN);
        }
        printf("STORE: flash empty or invalid, using defaults\n");
    }
    g.boot_count++;
    mark_dirty();
}
 
const store_data_t *store_get(void) { return &g; }
 
bool store_set_code(int slot0, const char *code)
{
    if (slot0 < 0 || slot0 >= STORE_NUM_CODES || code == NULL || strlen(code) != STORE_CODE_LEN) return false;
    char up[STORE_CODE_LEN + 1];
    for (int i = 0; i < STORE_CODE_LEN; i++) {
        char c = code[i];
        up[i] = (c >= 'a' && c <= 'f') ? (char)(c - 32) : c;
    }
    up[STORE_CODE_LEN] = '\0';
    if (!code_ok(up)) return false;
    memcpy(g.codes[slot0], up, sizeof up);
    mark_dirty();
    return true;
}
 
int store_match(const char *tag10)
{
    for (int i = 0; i < STORE_NUM_CODES; i++) {
        if (strncmp(g.codes[i], tag10, STORE_CODE_LEN) == 0) return i + 1;
    }
    return 0;
}
 
void store_log_add(const char *tag10, int slot)
{
    store_log_entry_t e;
    memset(&e, 0, sizeof e);
    memcpy(e.tag, tag10, STORE_CODE_LEN);
    e.authorized = slot > 0;
    e.slot = (uint8_t)slot;
    e.uptime_s = now_ms() / 1000;
    e.boot = g.boot_count;
 
    // Push everything back one place (the oldest falls off the end), newest goes in front
    memmove(&g.log[1], &g.log[0], (STORE_LOG_MAX - 1) * sizeof g.log[0]);
    g.log[0] = e;
    if (g.log_count < STORE_LOG_MAX) g.log_count++;
    mark_dirty();
}
 
void store_poll(void)
{
    static union {
        store_data_t d;
        uint8_t raw[STORE_FLASH_BYTES];
    } stage;
 
    cyw43_arch_lwip_begin();
    bool go = s_dirty && (int32_t)(now_ms() - s_dirty_since) >= FLUSH_DELAY_MS;
    if (go) {
        memset(stage.raw, 0xFF, sizeof stage.raw);
        memcpy(&stage.d, &g, sizeof g);
        stage.d.crc = calc_crc(&stage.d);
        s_dirty = false;
    }
    cyw43_arch_lwip_end();
    if (!go) return;
 
    // Erase + program with interrupts off (single core, nothing else may touch flash meanwhile).
    // Takes tens of milliseconds; the RFID reader repeats its frame every ~100 ms so nothing is lost.
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(STORE_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(STORE_FLASH_OFFSET, stage.raw, STORE_FLASH_BYTES);
    restore_interrupts(ints);
    printf("STORE: saved to flash\n");
}
 