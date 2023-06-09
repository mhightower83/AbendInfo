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
 */
#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include <AsyncPing.h>
#include "AbendNetworkHealth.h"

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
  Network RX/TX Hang detection
   * Ping the router to verify that transmiter is not hung.
*/

static struct NetworkMonitor {
    IPAddress ip = 0;
    uint32_t  last_ok;
    uint32_t  interval;         // ping repeat rate
    uint32_t  rx_count = 0;
    bool      ping_complete = false;
    bool      enabled = false;
    bool      up = false;
    bool      restart = false;
    AsyncPing ping;
} netmon;

// Do a ping (with 3 retries) every 2 minutes
constexpr uint32_t kPingInterval = 2u*60u*1000u;

// After 20 minutes of failed pings set restart
constexpr uint32_t kTimeout_Restart = 20u*60u*1000u;

bool abendIsNetworkOK(void) {
    if (! netmon.enabled) return true;
    uint32_t now = millis();
    if (now - netmon.interval < kPingInterval) return true;
    netmon.interval = now;

    netmon.ip = WiFi.localIP();
    if (netmon.up) {
        if (netmon.ip.isSet()) {
            if (netmon.ping_complete) {
                if (netmon.rx_count) {
                    netmon.rx_count = 0;
                    netmon.last_ok = now;
                    netmon.restart = false;
                }
                netmon.ping_complete = false;
                netmon.ping.begin(WiFi.gatewayIP());
            } else {
                // kPingInterval is much larger than the time for 3 ping packets
                ets_uart_printf("\nping_complete should have already finished\n");
            }
        } else {
            netmon.up       = false;
            netmon.rx_count = 0;
            netmon.ping.cancel();  // TODO is there anything else that needs to be done?
        }
    } else {
        if (netmon.ip.isSet()) {
            netmon.up       = true;
            netmon.rx_count = 0;
            netmon.last_ok  = now;
            netmon.restart  = false;

            netmon.ping.on(true, [](const AsyncPingResponse& response) {
                if (response.answer) {
                    netmon.rx_count++;
                    return true;  // done - Stop - one is enough
                }
                return false;     // don't stop - try again
            });

            netmon.ping.on(false, [](const AsyncPingResponse& response) {
                // Final result
                (void)response;
                netmon.ping_complete = true;
                return true;  // stop
            });
            netmon.ping_complete = true; // kick start the process
            netmon.interval = now - kPingInterval;
        }
    }

    if (netmon.up && (now - netmon.last_ok) > kTimeout_Restart) {
        netmon.restart = true;
    }

    return !netmon.restart;
}


void abendEnableNetworkMonitor(bool enable) {
    if (netmon.enabled == enable) return;

    uint32_t now = millis();
    netmon.interval = now - kPingInterval;
    netmon.enabled = enable;

    if (netmon.enabled) {
        // empty
    } else {
        netmon.ping.cancel();
    }
    // To avoid reseting before we get started, always start in the down state
    // and detect Network is up in the poll loop.
    netmon.up       = false;
    netmon.last_ok  = now;
    netmon.restart  = false;
    netmon.rx_count = 0;
}

void abendShowNetworkHealth(Print& sio) {
    sio.printf_P(PSTR("\nNetwork Health %S\r\n"), (netmon.enabled) ? "" : "Monitor Disabled");
    if (netmon.enabled) {
        sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Interface up:"), (netmon.up) ? "true" : "false");
        // sio.printf_P(PSTR("  %-23S %5u\r\n"), PSTR("RX Count:"), netmon.rx_count);
        sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Ping complete:"), (netmon.ping_complete) ? "true" : "false");
        if (netmon.ping_complete) {
            sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Ping:"), (netmon.rx_count) ? "success" : "failed");
        }
        sio.printf_P(PSTR("  %-23S %s\r\n"), PSTR("Restart:"), (netmon.restart) ? "true" : "false");
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
        // SHOW_PRINTF("  %-23s %5u\r\n", "RX Count:", netmon.rx_count);
        SHOW_PRINTF("  %-23S %s\r\n", "Ping complete:", (netmon.ping_complete) ? "true" : "false");
        if (netmon.ping_complete) {
            SHOW_PRINTF("  %-23s %s\r\n", "Ping:", (netmon.rx_count) ? "success" : "failed");
        }
        SHOW_PRINTF("  %-23s %s\r\n", "Restart:", (netmon.restart) ? "true" : "false");
    }
}

#else
void abendEnableNetworkMonitor(bool enable) { (void)enable; }
bool abendIsNetworkOK(void) { return true; }
void abendShowNetworkHealth(Print& sio) { (void)sio; }
#endif // WIP
