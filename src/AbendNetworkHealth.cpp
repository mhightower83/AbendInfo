/*
 *   Copyright 2023 M Hightower
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
/*
 * Monitor Network Health
 *
 * Summary:
 *   * Monitor for WiFi RX/TX hang - using AsyncPing
 *
 * Additional library required:
 *   * https://github.com/akaJes/AsyncPing.git
 *
 * TODO: (maybe)
 *   This method would generate the least amount of Network traffic and is not
 *   dependent on a target responding to pings.
 *   Look at using `etharp_get_entry()` to scan the ARP table (0 - ARP_TABLE_SIZE) for entries.
 *   When zero entries, use `etharp_request()` to send a request to the default router
 *   Count the number of consecutive times ARP table is empty. After n, generate error event.
 */
#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

//D #include <AsyncPing.h>
#include <lwip/etharp.h>
#include "AbendNetworkHealth.h"

extern "C" {
  #include <lwip/sys.h>
  // Extracted from etharp.c
  /** ARP states */
  enum etharp_state {
    ETHARP_STATE_EMPTY = 0,
    ETHARP_STATE_PENDING,
    ETHARP_STATE_STABLE,
    ETHARP_STATE_STABLE_REREQUESTING_1,
    ETHARP_STATE_STABLE_REREQUESTING_2
  #if ETHARP_SUPPORT_STATIC_ENTRIES
    , ETHARP_STATE_STATIC
  #endif /* ETHARP_SUPPORT_STATIC_ENTRIES */
  };

  struct etharp_entry {
  #if ARP_QUEUEING
    /** Pointer to queue of pending outgoing packets on this ARP entry. */
    struct etharp_q_entry *q;
  #else /* ARP_QUEUEING */
    /** Pointer to a single pending outgoing packet on this ARP entry. */
    struct pbuf *q;
  #endif /* ARP_QUEUEING */
    ip4_addr_t ipaddr;
    struct netif *netif;
    struct eth_addr ethaddr;
    u16_t ctime;
    u8_t state;
  };
}


#pragma GCC optimize("Os")


////////////////////////////////////////////////////////////////////////////////
//
#if ABENDINFO_NETWORK_MONITOR
#ifndef QUOTE
#define QUOTE(a) __STRINGIFY(a)
#endif

extern "C" int umm_info_safe_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define ETS_PRINTF(fmt, ...) umm_info_safe_printf_P(PSTR(fmt), ##__VA_ARGS__)
#if ABENDINFO_POSTMORTEM_EXTRA
// Used to provide additional info after Postmortem report at custom crash callback.
#define SHOW_PRINTF ETS_PRINTF
#else
#define SHOW_PRINTF(fmt, ...)
#endif

/*
  int etharp_get_entry(
    size_t i,
    ip4_addr_t**ipaddr,
    struct netif** netif,
    struct eth_addr ** eth_ret
  );
  Parameters
      i       entry number, 0 to ARP_TABLE_SIZE
      ipaddr  return value: IP address
      netif   return value: points to interface
      eth_ret	return value: ETH address
  Returns
      1   on valid index,
      0   otherwise
*/
static size_t etharpGetCount(void) {
    // etharp_get_entry()` to scan the ARP table (0 - ARP_TABLE_SIZE) for entries.
    [[maybe_unused]] ip4_addr_t      *ipaddr;
    [[maybe_unused]] struct netif    *netif;
    [[maybe_unused]] struct eth_addr *eth_ret;

    size_t count = 0;
    for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
        // etharp_get_entry() => https://www.nongnu.org/lwip/2_1_x/lwip_2etharp_8h.html#ab93df7ccb26496100d45137541e863c8
        if (etharp_get_entry(i, &ipaddr, &netif, &eth_ret)) {
            count++;
        }
    }
    return count;
}

#if ABENDINFO_DEBUG
inline uint8_t getByte(size_t n, uint32_t val) {
    return (uint8_t)(val >> (n*8));
}

static void printIP(ip4_addr_t *ipaddr) {
    uint32_t val = ipaddr->addr;
    SHOW_PRINTF((" %u.%u.%u.%u"),
        getByte(0, val), getByte(1, val), getByte(2, val), getByte(3, val));
}
#endif

/*
  Network RX/TX Hang detection
   * Use ARP to ping the router to verify that transmiter is not hung.
*/

static struct NetworkMonitor {
    IPAddress     ip = 0;
    uint32_t      last_ok;
    uint32_t      interval;         // ping repeat rate
    size_t        pbuf_err;         // count pbuf not available
    struct netif* netif = NULL;
    size_t        ctime_no_change = 0;
    u16_t         ctime_last;
    bool          enabled = false;
    bool          up = false;
    bool          restart = false;
} netmon;

// Do a ping (with 3 retries) every 2 minutes
constexpr uint32_t kPingInterval = 2u*60u*1000u;

// After 20 minutes of failed pings set restart
constexpr uint32_t kTimeout_Restart = 20u*60u*1000u;

// Use pointer of IP Address to find begining of etharp_entry structure
static inline const struct etharp_entry *getArpEntryFromIpPtr(const ip4_addr_t *ip_ret) {
    const struct etharp_entry *pt = (const struct etharp_entry *)
        ((uintptr_t)ip_ret - offsetof(struct etharp_entry, ipaddr));
    return pt;
}
/*
err_t etharp_request(struct netif *netif, const ip4_addr_t * ipaddr)
ERR_OK - on success

ssize_t etharp_find_addr(
struct netif *      netif,
const ip4_addr_t *  ipaddr,
struct eth_addr **  eth_ret,
const ip4_addr_t ** ip_ret
)

  We are looking for a stuck state that appears unrecoverable. Things like
  Network Hardware hangs (possible the result of undocumented hardware errata),
  etc.
  1) A short period of the interface being down is left as normal event.
     However, prolonged state of No IP Address after kTimeout_Restart time
     (20 minutes) is not.
  2) Interface being up and ARP table empty and GW not responding to ARPs

*/
bool abendIsNetworkOK(void) {
    if (! netmon.enabled) return true;
    uint32_t now = millis();
    if (now - netmon.interval < kPingInterval) return true;
    // Performed about every 2 minutes
    netmon.interval = now;

    netmon.ip = WiFi.localIP();
    err_t err = ERR_OK;
    if (netmon.up) {
        if (netmon.ip.isSet()) {
            [[maybe_unused]] struct eth_addr *eth_ret;
            [[maybe_unused]] const ip4_addr_t *ip_ret;
            ssize_t idx =
            etharp_find_addr(netmon.netif, &netmon.netif->gw, &eth_ret, &ip_ret);
            if (0 <= idx) {
                const struct etharp_entry *arp = getArpEntryFromIpPtr(ip_ret);
                // We could look for ETHARP_STATE_STABLE or above; however, if
                // things are working well those states should be short lived
                // and the state should return to ETHARP_STATE_STABLE.
                if (ETHARP_STATE_STABLE == arp->state) {
                    if (arp->ctime == netmon.ctime_last) {
                        netmon.ctime_no_change++;
                    } else {
                        netmon.ctime_last = arp->ctime;
                        netmon.ctime_no_change = 0;
                        netmon.last_ok = now;
                        netmon.restart = false;
                    }
                }
                struct pbuf *pbuf = pbuf_alloc(PBUF_LINK, SIZEOF_ETHARP_HDR, PBUF_RAM);
                if (pbuf) {
                    pbuf_free(pbuf);
                    pbuf = NULL;
                } else {
                    err = ERR_MEM;
                }
            } else {
                err = etharp_request(netmon.netif, &netmon.netif->gw);
                // Rely on later checks for '(now - netmon.last_ok) > kTimeout_Restart)'
                // to flag a problem.
            }
            if (ERR_OK != err){
                netmon.pbuf_err++;
            }
        } else {
            netmon.up = false;
        }
    } else {
        if (netmon.ip.isSet()) {
            netmon.up       = true;
            netmon.last_ok  = now;
            netmon.restart  = false;
            netmon.interval = now - kPingInterval; // lets start with a ping
            netmon.ctime_no_change = 0;
            netmon.ctime_last = UINT16_MAX;
            netmon.netif = NULL;
            // Locate netif holding our IP Address.
            for (netif* interface = netif_list; interface != nullptr; interface = interface->next) {
                if (interface->ip_addr.addr != netmon.ip.v4()) continue;
                netmon.netif = interface;
#if ABENDINFO_DEBUG
                SHOW_PRINTF("\r\n%3u netif: 0x%02X", interface->num, interface->flags);
                printIP(&interface->ip_addr);
                printIP(&interface->netmask);
                printIP(&interface->gw);
                SHOW_PRINTF("\r\n");
#endif
                break;
            }
        }
    }

    // if (netmon.up && (now - netmon.last_ok) > kTimeout_Restart) { // 20 minutes to reset
    if ((now - netmon.last_ok) > kTimeout_Restart) { // 20 minutes to reset
        // Before rebooting, confirm the Network interface's hung state by
        // waiting until all ARP cache entries have expired.
        // LWIP build defaults ARP entry timeout at 5 minutes.

        //+ netmon.restart = (0 == etharpGetCount());
        //D still trying to get a handle on why IRsender emulating 10 Wemo devices becomes non-responseive
        netmon.restart = true;
        // Confirmed, with 20 minutes of AP w/o a Network connection will return false.
    }

    return !netmon.restart;
}

size_t abendGetArpCount(void) {
    return etharpGetCount();
}


void abendEnableNetworkMonitor(bool enable) {
    if (netmon.enabled == enable) return;

    uint32_t now = millis();
    netmon.interval = now - kPingInterval;
    netmon.enabled  = enable;
    netmon.netif    = NULL;
    // To avoid reseting before we get started, always start in the down state
    // and detect Network is up in the poll loop.
    netmon.up       = false;
    netmon.last_ok  = now;
    netmon.restart  = false;
    // netmon.ctime_no_change = 0;
    // netmon.ctime_last = UINT16_MAX;
}

void abendShowNetworkHealth(Print& sio) {
    sio.printf_P(PSTR("\nNetwork Health %S\r\n"), (netmon.enabled) ? "" : "Monitor Disabled");
    if (netmon.enabled) {
        sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Interface up:"), (netmon.up) ? "true" : "false");
        sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Restart:"), (netmon.restart) ? "true" : "false");
    }
    if (netmon.pbuf_err) {
        sio.printf_P(PSTR("  %-23S %u\r\n"), PSTR("No pbuf count:"), netmon.pbuf_err);
    }
}

extern "C" void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH(
    struct rst_info *rst_info, uint32_t stack, uint32_t stack_end) {
    (void)rst_info;
    (void)stack;
    (void)stack_end;

    SHOW_PRINTF("\nNetwork Health %s\r\n", (netmon.enabled) ? "" : "Monitor Disabled");
    if (netmon.enabled) {
        SHOW_PRINTF("  %-23s %s\r\n", "Interface up:", (netmon.up) ? "true" : "false");
        SHOW_PRINTF("  %-23s %s\r\n", "Restart:", (netmon.restart) ? "true" : "false");
    }
    if (netmon.pbuf_err) {
        SHOW_PRINTF("  %-23S %u\r\n", "No pbuf count:", netmon.pbuf_err);
    }
}

#else
void abendEnableNetworkMonitor(bool enable) { (void)enable; }
bool abendIsNetworkOK(void) { return true; }
void abendShowNetworkHealth(Print& sio) { (void)sio; }
size_t abendGetArpCount(void) { return 0; }
#endif // WIP
