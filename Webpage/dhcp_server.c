// Minimal DHCP server for lwIP (NO_SYS, raw UDP API).
// Handles DISCOVER -> OFFER and REQUEST -> ACK, /24 network.

#include <string.h>
#include <stdint.h>
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "dhcp_server.h"

#define DHCPS_PORT       67
#define DHCPC_PORT       68
#define POOL_START       16
#define POOL_SIZE        8
#define LEASE_SECONDS    86400

#define DHCP_DISCOVER    1
#define DHCP_OFFER       2
#define DHCP_REQUEST     3
#define DHCP_ACK         5

#define OPT_SUBNET       1
#define OPT_ROUTER       3
#define OPT_DNS          6
#define OPT_LEASE_TIME   51
#define OPT_MSG_TYPE     53
#define OPT_SERVER_ID    54
#define OPT_END          255

typedef struct __attribute__((packed)) {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint8_t  ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint8_t  magic[4];
    uint8_t  options[308];
} dhcp_msg_t;

#define DHCP_FIXED_LEN   (sizeof(dhcp_msg_t) - sizeof(((dhcp_msg_t *)0)->options))

static struct udp_pcb *s_pcb;
static struct netif   *s_netif;
static uint8_t s_lease_mac[POOL_SIZE][6];
static bool    s_lease_used[POOL_SIZE];

static int lease_for_mac(const uint8_t *mac) {
    int free_slot = -1;
    for (int i = 0; i < POOL_SIZE; i++) {
        if (s_lease_used[i] && memcmp(s_lease_mac[i], mac, 6) == 0) return i;
        if (!s_lease_used[i] && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) free_slot = 0;  // pool full: recycle the first slot
    memcpy(s_lease_mac[free_slot], mac, 6);
    s_lease_used[free_slot] = true;
    return free_slot;
}

static uint8_t *add_opt(uint8_t *p, uint8_t code, uint8_t len, const void *data) {
    *p++ = code;
    *p++ = len;
    memcpy(p, data, len);
    return p + len;
}

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)addr; (void)port;
    dhcp_msg_t req;

    if (p->tot_len < DHCP_FIXED_LEN + 4 || p->tot_len > sizeof(req)) {
        pbuf_free(p);
        return;
    }
    memset(&req, 0, sizeof(req));
    pbuf_copy_partial(p, &req, p->tot_len, 0);
    pbuf_free(p);

    if (req.op != 1 || req.hlen != 6 ||
        req.magic[0] != 99 || req.magic[1] != 130 ||
        req.magic[2] != 83 || req.magic[3] != 99) return;

    // Find message type option
    uint8_t msg_type = 0;
    for (uint8_t *o = req.options; o < req.options + sizeof(req.options) && *o != OPT_END; ) {
        if (*o == 0) { o++; continue; }          // pad
        if (o + 1 >= req.options + sizeof(req.options)) break;
        uint8_t code = o[0], len = o[1];
        if (code == OPT_MSG_TYPE && len == 1) msg_type = o[2];
        o += 2 + len;
    }

    uint8_t reply_type;
    if (msg_type == DHCP_DISCOVER)      reply_type = DHCP_OFFER;
    else if (msg_type == DHCP_REQUEST)  reply_type = DHCP_ACK;
    else return;

    uint8_t server_ip[4], mask[4];
    memcpy(server_ip, netif_ip4_addr(s_netif), 4);
    memcpy(mask, netif_ip4_netmask(s_netif), 4);

    uint8_t client_ip[4];
    memcpy(client_ip, server_ip, 4);
    client_ip[3] = POOL_START + lease_for_mac(req.chaddr);

    dhcp_msg_t rep;
    memset(&rep, 0, sizeof(rep));
    rep.op = 2; rep.htype = req.htype; rep.hlen = req.hlen;
    rep.xid = req.xid; rep.flags = req.flags;
    memcpy(rep.yiaddr, client_ip, 4);
    memcpy(rep.siaddr, server_ip, 4);
    memcpy(rep.chaddr, req.chaddr, 16);
    rep.magic[0] = 99; rep.magic[1] = 130; rep.magic[2] = 83; rep.magic[3] = 99;

    uint8_t *o = rep.options;
    uint32_t lease_be = lwip_htonl(LEASE_SECONDS);
    o = add_opt(o, OPT_MSG_TYPE, 1, &reply_type);
    o = add_opt(o, OPT_SERVER_ID, 4, server_ip);
    o = add_opt(o, OPT_LEASE_TIME, 4, &lease_be);
    o = add_opt(o, OPT_SUBNET, 4, mask);
    o = add_opt(o, OPT_ROUTER, 4, server_ip);
    o = add_opt(o, OPT_DNS, 4, server_ip);
    *o++ = OPT_END;

    u16_t out_len = (u16_t)(o - (uint8_t *)&rep);
    struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, out_len, PBUF_RAM);
    if (!q) return;
    pbuf_take(q, &rep, out_len);
    udp_sendto_if(pcb, q, IP_ADDR_BROADCAST, DHCPC_PORT, s_netif);
    pbuf_free(q);
}

void dhcp_server_start(struct netif *netif) {
    s_netif = netif;
    memset(s_lease_used, 0, sizeof(s_lease_used));
    s_pcb = udp_new();
    if (!s_pcb) return;
    udp_bind(s_pcb, IP_ANY_TYPE, DHCPS_PORT);
    udp_recv(s_pcb, dhcp_recv, NULL);
}
