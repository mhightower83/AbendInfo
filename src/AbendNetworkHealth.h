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
#ifndef ABENDSYSTEMHEALTH_H
#define ABENDSYSTEMHEALTH_H

#include "AbendInfo.h"
#include <lwip/err.h>


#if !defined(ABENDINFO_NETWORK_MONITOR) && defined(SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH)
#define ABENDINFO_NETWORK_MONITOR 1
#else
/*
  Because AbendInfo.h and AbendNetworkHealth.h are in the same directory when
  AbendInfo.h is included in a Sketch, AbendNetworkHealth.cpp will also be
  built. To prevent build errors, we need to block building AbendNetworkHealth.
*/
#define ABENDINFO_NETWORK_MONITOR 0
#endif

#if ABENDINFO_NETWORK_MONITOR
#ifndef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH
/*
  This will create a linker conflict with duplicate custom_crash_callback.
  You must create a custom_crash_callback() in your Sketch to hold shared Abend
  and AbendNetworkHealth custom crash callbacks see examples.
*/
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH custom_crash_callback
#endif
extern "C" void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);

#else
#undef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH(...);
#endif

err_t abendCheckNetwork(void);
bool abendIsNetworkOK(void);
void abendShowNetworkHealth(Print& sio);
void abendEnableNetworkMonitor(bool enable);
// size_t abendGetArpCount(void);

void reportEbCxt(Print& sio);
void report_ebCxt(void);
// bool getnL32rValue(uintptr_t pf, int skip, void **literalValue, bool debug=false);

#endif // ABENDSYSTEMHEALTH_H
