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
#include <lwip/etharp.h>
#include "AbendNetworkHealth.h"

// Do a Network Health Check every 2 minutes
constexpr uint32_t kNetChkInterval = 2u*60u*1000u;

// After 20 minutes of failed Network Health Checks set restart
constexpr uint32_t kTimeoutRestart = 20u*60u*1000u;


extern "C" {
  #include <lwip/sys.h>
  #include <lwip/pbuf.h>
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
    struct etharp_q_entry *q;   // +0
  #else /* ARP_QUEUEING */
    /** Pointer to a single pending outgoing packet on this ARP entry. */
    struct pbuf *q;
  #endif /* ARP_QUEUEING */
    ip4_addr_t ipaddr;          // +4
    struct netif *netif;        // +8
    struct eth_addr ethaddr;    // +12
    u16_t ctime;                // +18
    u8_t state;                 // +20
  };

  ///////////////////////////////////////////////////////////////////
  // WiFi buffer pools

  struct esf_buf {
      struct pbuf *pb1;                       /*  0 */
      struct pbuf *pb2;                       /*  4 */ // = malloc(0xc) // type 4,
      struct pbuf *pb3;                       /*  8 */ // same ptr as above
      uint16 cnt1;                            /* 12 */ // init: 1 // type 4
      uint8 flg;                              /* 14 */
      uint8 pad1[1];
      struct ieee80211_frame *e_data;         /* 16 */ // = malloc(3rd arg to esf_buf_alloc) // type 4
      uint16 len1;                            /* 20 */
      uint16 len2;                            /* 22 */
      uint8 pad2[4];
      uint32 type1;                           /* 28 */
      struct esf_buf *next;                   /* 32 */
      // points to struct esf_buf_subN, depending on buf type
      void *ext;                              /* 36 0x24 */
  };

  struct private_esf_buf_pools {
      struct esf_buf *pool_1;         // 0  - Type 1 and 2
      struct esf_buf *pool_unknown;   // 4  - Looks unused in SDK 3.0.5
      struct esf_buf *pool_5;         // 8  - Type 5
      struct esf_buf *pool_7;         // 12 - Type 7
      struct esf_buf *rx_pool_8;      // 16 - type 8    // esf_rx_buf_alloc(8) called from wDev_IndicateFrame
      // +0x14, +20
      // This field is decremented by esf_rx_buf_alloc, but initialized to 0
      // and never incremented. So, it's negated number of allocated rx bufs.
      // This may be usefull for detecting RX Hang
      uint32_t rxblock_cnt; // extern uint32_t g_free_rxblock_eb_cnt_32; local to libpp.a
  } *p_ebCxt = NULL;

  // https://github.com/pfalcon/esp-open-headers/blob/master/pp/esf_buf.h
  struct esf_buf *esf_buf_alloc(struct pbuf *pbuf, u32 buf_type, u32 size_of_data_buf);
  void esf_buf_recycle(struct esf_buf *buf, u32 buf_type);
  void esf_buf_setup(void);

} // extern "C" {

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


////////////////////////////////////////////////////////////////////////////////
// Gain access to module static data elements
// Generate reports on free Private WiFi RX/TX memory pools.

/*
  Access private data elements by fetching the "literal" data element used
  by a function to access the data element. This is commonly a `l32r` instruction.
*/

/*
  Check instruction at epc for `l32r` opcode and on true return pointer to
  literal table entry.

  epc    - IRAM/ICACHE address to inspect for `l32r` instruction.

  *_insn - location to save the instruction found at epc

  When opcode is `l32r`, returns pointer to literal table entry in the
  IRAM/ICACHE address space, otherwise returns NULL.

  Assumptions, litbase is 0.
*/
uintptr_t getL32rLiteralPtr(uintptr_t epc, uint32_t* _insn) { //pf), uint32_t l32rOffset) {
    struct RI16_LI_S { // l32r
        uint32_t op0:4;
        uint32_t t:4;
        int32_t imm16:16;
    };
    union {
        struct RI16_LI_S l32r;
        uint32_t u32 = 0;
    } l32r_src;

    uint64_t big_word = *(uint64_t *)(epc & ~3);
    uint32_t pos = (epc & 3) * 8;
    l32r_src.u32 = (uint32_t)(big_word >>= pos);
    *_insn = l32r_src.u32;
    if (0x01 != l32r_src.l32r.op0) {
        // Not an l32r instruction
        return 0;
    }
    ssize_t literalAddr = (ssize_t)((epc + 3) & ~3);
    literalAddr += l32r_src.l32r.imm16 << 2;
    return (uintptr_t)literalAddr;    // an IRAM pointer at a "literal" value
}

/*
  Get literal value at start of function

  pf           - pointer to the start of the function

  skip         - {0, 1, ...} the number of `l32r` instructions to skip over.

  literalValue - the address to save the 32-bit value from the literal table.

  returns true on success.
    Fails on exceeding skip value `skip` or first `ret` instruction found.
    (or built in search limit)
*/
bool getnL32rValue(uintptr_t pf, int skip, void **literalValue, bool debug=false) {
    const uint32_t limitSearch = skip * (3 + 9) + 64;
    union {
        struct {
            uint32_t u24:24;
            uint32_t pad:8;
        } op3;
        struct {
            uint32_t u16:16;
            uint32_t pad:16;
        } op2;
        uint32_t u32;
    } insn;
    void **literalAddr;
    for (uint32_t i = 0; i < limitSearch;) {
        literalAddr = (void**)getL32rLiteralPtr(pf + (uintptr_t)i, &insn.u32);
        if (debug) ETS_PRINTF("epc: %p, literalAddr: %p\r\n", pf + (uintptr_t)i, literalAddr);
        if (literalAddr && 0 == (3u & (uintptr_t)literalAddr)) {
            // matched l32r instruction
            if (0 == skip) {
                *literalValue = *literalAddr;
                return true;
            }
            skip -= 1;
        } else
        if (0x000080 == insn.op3.u24 || 0xf00d == insn.op2.u16) {
            // matched ret or ret.n, give up search
            if (debug) ETS_PRINTF("getnL32rValue: Found ret\r\n");
            return false;
        }
        // Advance to next instruction
        if (insn.op2.u16 & 0x08u) {
            i += 2;
        } else {
            i += 3;
        }
    }
    if (debug) ETS_PRINTF("getnL32rValue: reached limitSearch\r\n");
    return false;
}

struct ReportEbCxtCnt {
    uint32_t pool_1        = 0;
    uint32_t pool_unknown  = 0;
    uint32_t pool_5        = 0;
    uint32_t pool_7        = 0;
    uint32_t rx_pool_8     = 0;
    uint32_t rxblock_cnt   = 0;
};


bool initEbCxtPtr(void) {
    // Get address for WiFi RX/TX memory pools managed by esf_buf_alloc/esf_buf_recycle
    return getnL32rValue((uintptr_t)esf_buf_alloc, 0, (void**)&p_ebCxt); //, true);
}

/*
  Tally up available esf_bufs in the specified pool.
*/
uint32_t freeCount(struct esf_buf* p) {
    uint32_t count = 0;
    while(p) {
        count++;
        p = p->next;
    }
    return count;
}

bool getEbCxtStats(struct ReportEbCxtCnt *ebCnt) {
    bool ok = false;
    uint32_t save_ps = xt_rsil(15);
    if (p_ebCxt) {
        ok = true;
        ebCnt->pool_1        = freeCount(p_ebCxt->pool_1);
        ebCnt->pool_unknown  = freeCount(p_ebCxt->pool_unknown);
        ebCnt->pool_5        = freeCount(p_ebCxt->pool_5);
        ebCnt->pool_7        = freeCount(p_ebCxt->pool_7);
        ebCnt->rx_pool_8     = freeCount(p_ebCxt->rx_pool_8);
        ebCnt->rxblock_cnt   = p_ebCxt->rxblock_cnt;
    }
    xt_wsr_ps(save_ps);
    return ok;
}

// If returned value is always changing, then WiFi RX is not hung.
uint32_t getRxBlockCnt() {
    uint32_t cnt = 0;
    if (p_ebCxt) {
        uint32_t save_ps = xt_rsil(15);
        cnt = p_ebCxt->rxblock_cnt;
        xt_wsr_ps(save_ps);
    }
    return cnt;
}

void reportEbCxt(Print& sio) {
    if (NULL == p_ebCxt) initEbCxtPtr();

    struct ReportEbCxtCnt ebCxt;
    if (getEbCxtStats(&ebCxt)) {
        sio.printf_P(PSTR("\nESP WiFi buffer pools\r\n"));
        sio.printf_P(PSTR("  %-20S %2u/8\r\n"),  ("pool_1"),       ebCxt.pool_1);
        if (ebCxt.pool_unknown) // Looks unused
        sio.printf_P(PSTR("  %-20S %2u/?\r\n"),  ("pool_unknown"), ebCxt.pool_unknown);
        sio.printf_P(PSTR("  %-20S %2u/8\r\n"),  ("pool_5"),       ebCxt.pool_5);
        sio.printf_P(PSTR("  %-20S %2u/4\r\n"),  ("pool_7"),       ebCxt.pool_7);
        sio.printf_P(PSTR("  %-20S %2u/7\r\n"),  ("rx_pool_8"),    ebCxt.rx_pool_8);
        sio.printf_P(PSTR("  %-20S 0x%08X\r\n"), ("rxblock_cnt"),  ebCxt.rxblock_cnt );
    }
}

void report_ebCxt(void) {
    struct ReportEbCxtCnt ebCxt;
    if (getEbCxtStats(&ebCxt)) {
        ETS_PRINTF("\nESP WiFi buffer pools\r\n");
        ETS_PRINTF("  %-20s %2u/8\r\n",  ("pool_1"),       ebCxt.pool_1);
        if (ebCxt.pool_unknown) // Looks unused
        ETS_PRINTF("  %-20s %2u/?\r\n",  ("pool_unknown"), ebCxt.pool_unknown);
        ETS_PRINTF("  %-20s %2u/8\r\n",  ("pool_5"),       ebCxt.pool_5);
        ETS_PRINTF("  %-20s %2u/4\r\n",  ("pool_7"),       ebCxt.pool_7);
        ETS_PRINTF("  %-20s %2u/7\r\n",  ("rx_pool_8"),    ebCxt.rx_pool_8);
        ETS_PRINTF("  %-20s 0x%08X\r\n", ("rxblock_cnt"),  ebCxt.rxblock_cnt );
    }
}


/*
  Function in lwip library

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
// static size_t etharpGetCount(void) {
//     // etharp_get_entry()` to scan the ARP table (0 - ARP_TABLE_SIZE) for entries.
//     [[maybe_unused]] ip4_addr_t      *ipaddr;
//     [[maybe_unused]] struct netif    *netif;
//     [[maybe_unused]] struct eth_addr *eth_ret;
//
//     size_t count = 0;
//     for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
//         // etharp_get_entry() => https://www.nongnu.org/lwip/2_1_x/lwip_2etharp_8h.html#ab93df7ccb26496100d45137541e863c8
//         if (etharp_get_entry(i, &ipaddr, &netif, &eth_ret)) {
//             count++;
//         }
//     }
//     return count;
// }

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
    uint32_t      interval;         // ARP ping repeat rate
    size_t        pbuf_err;         // count pbuf not available
    struct netif* netif = NULL;
    uint32_t      rx_last_ok;
    uint32_t      rx_cnt_last;
    size_t        rx_cnt_no_change;
    err_t         err = ERR_OK;
    bool          enabled = false;
    bool          up = false;
    bool          restart = false;
} netmon;

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
     However, prolonged state of No IP Address after kTimeoutRestart time
     (20 minutes) is not.
  2) Interface being up and GW not responding to ARPs
  3) Use a stuck RX Block Count maintained by esf_rx_buf_alloc() to detect a
     RX Hang.
*/
err_t abendCheckNetwork(void) {
    if (! netmon.enabled) return ERR_OK;
    uint32_t now = millis();
    if (now - netmon.interval < kNetChkInterval) return ERR_OK;
    // Performed about every 2 minutes
    netmon.interval = now;

    netmon.ip = WiFi.localIP();
    netmon.err = ERR_OK;
    if (netmon.up) {
        if (netmon.ip.isSet()) {
            // Use RX Block Count to detect a RX Hang
            uint32_t rx_cnt = getRxBlockCnt();
            if (rx_cnt == netmon.rx_cnt_last) {
                netmon.rx_cnt_no_change++;
            } else [[likely]] {
                netmon.rx_cnt_last = rx_cnt;
                netmon.rx_cnt_no_change = 0;
                netmon.rx_last_ok = now;
            }

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
                    netmon.last_ok = now;
                }
                struct pbuf *pbuf = pbuf_alloc(PBUF_LINK, SIZEOF_ETHARP_HDR, PBUF_RAM);
                if (pbuf) {
                    pbuf_free(pbuf);
                    pbuf = NULL;
                } else {
                    if (ERR_OK == netmon.err) netmon.err = ERR_MEM;
                }
            } else {
                netmon.err = etharp_request(netmon.netif, &netmon.netif->gw);
                // Rely on later checks for '(now - netmon.last_ok) > kTimeoutRestart)'
                // to flag a problem.
                if (ERR_OK == netmon.err) netmon.err = ERR_INPROGRESS;
            }
            if (ERR_MEM == netmon.err) netmon.pbuf_err++;
            if (ERR_OK == netmon.err && netmon.rx_cnt_no_change) netmon.err = ERR_IF;
        } else {
            netmon.up = false;
            netmon.err = ERR_CLSD;
        }
    } else {
        if (netmon.ip.isSet()) {
            netmon.up       = true;
            netmon.last_ok  = now;
            netmon.restart  = false;
            netmon.interval = now - kNetChkInterval; // lets start with a Network Check
            netmon.netif = NULL;
            netmon.err = ERR_OK;
            netmon.rx_cnt_last = 0;   // This will get set correctly at the next loop
            netmon.rx_cnt_no_change = 0;
            netmon.rx_last_ok = now;
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

    //?? if (netmon.up && (now - netmon.last_ok) > kTimeoutRestart) { // 20 minutes to reset
    if ((now - netmon.last_ok)    > kTimeoutRestart
    ||  (now - netmon.rx_last_ok) > kTimeoutRestart) { // 20 minutes to reset
        // Before rebooting, confirm the Network interface's hung state by
        // waiting until all ARP cache entries have expired.
        // LWIP build defaults ARP entry timeout at 5 minutes.

        //+ netmon.restart = (0 == etharpGetCount());
        // still trying to get a handle on why IRsender emulating 10 Wemo devices
        // becomes non-responseive. Maybe not true anymore. Ran for 5 days with 10 devices. (??)
        // At fail we have:
        // 1) etharpGetCount() is not zero, reboot when kTimeoutRestart elapses
        // 2) The Network is up - we have an IP Address
        // ?) Are there any pbuf's available ?
        // ?) Is the scheduled timer callback running ?
        // ?)
        netmon.restart = true;
        // Confirmed, with 20 minutes of AP availableBytes w/o a Station Network
        // connection will return false.
        if (ERR_OK == netmon.err) netmon.err = ERR_TIMEOUT; // or keep previous error
        return netmon.err;
    }

    // return !netmon.restart;
    return ERR_OK;
}

bool abendIsNetworkOK(void) {
    return (ERR_OK == abendCheckNetwork());
}

// size_t abendGetArpCount(void) {
//     return etharpGetCount();
// }


void abendEnableNetworkMonitor(bool enable) {
    initEbCxtPtr();
    if (netmon.enabled == enable) return;

    uint32_t now = millis();
    netmon.interval = now - kNetChkInterval;
    netmon.enabled  = enable;
    netmon.netif    = NULL;
    // To avoid reseting before we get started, always start in the down state
    // and detect Network is up in the poll loop.
    netmon.up       = false;
    netmon.last_ok  = now;
    netmon.restart  = false;
    netmon.rx_cnt_last = getRxBlockCnt();
    netmon.rx_cnt_no_change = 0;
    netmon.rx_last_ok = now;
}

void abendShowNetworkHealth(Print& sio) {
    sio.printf_P(PSTR("\nNetwork Health %S\r\n"), (netmon.enabled) ? "" : "Monitor Disabled");
    if (netmon.enabled) {
        sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Interface up:"), (netmon.up) ? "true" : "false");
        sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Restart:"), (netmon.restart) ? "true" : "false");
    }
    if (netmon.rx_cnt_no_change) {
        sio.printf_P(PSTR("  %-23S 0x%08X\r\n"), PSTR("RX Block CNT stopped:"), netmon.rx_cnt_last);
    } else {
        sio.printf_P(PSTR("  %-23S %u\r\n"), PSTR("RX Block CNT:"), ~getRxBlockCnt() + 1);
    }
    if (netmon.err) {
        sio.printf_P(PSTR("  %-23S 0x%08X, %d\r\n"), PSTR("err_t:"), netmon.err, netmon.err);
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
    if (netmon.rx_cnt_no_change) {
        SHOW_PRINTF("  %-23s 0x%08X\r\n", "RX Block CNT stopped:", netmon.rx_cnt_last);
    } else {
        SHOW_PRINTF("  %-23s %u\r\n", "RX Block CNT:", ~getRxBlockCnt() + 1);
    }
    if (netmon.err) {
        SHOW_PRINTF("  %-23s 0x%08X, %d\r\n", "err_t:", netmon.err, netmon.err);
    }
    if (netmon.pbuf_err) {
        SHOW_PRINTF("  %-23s %u\r\n", "No pbuf count:", netmon.pbuf_err);
    }
    report_ebCxt();
}

#else
void abendEnableNetworkMonitor(bool enable) { (void)enable; }
bool abendIsNetworkOK(void) { return true; }
void abendShowNetworkHealth(Print& sio) { (void)sio; }
// size_t abendGetArpCount(void) { return 0; }
#endif // WIP
