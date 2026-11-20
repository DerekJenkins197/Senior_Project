#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

/* Starts a minimal DHCP server on the given netif (call with lwIP lock held).
 * Leases 192.168.4.16 .. 192.168.4.23 (last octet of server IP is ignored). */
void dhcp_server_start(struct netif *netif);

#endif
