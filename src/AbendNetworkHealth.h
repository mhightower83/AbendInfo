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

#if !defined(ABENDINFO_NETWORK_MONITOR) && ABENDINFO_HEAP_MONITOR
#define ABENDINFO_NETWORK_MONITOR 1
#endif

#if ABENDINFO_NETWORK_MONITOR
#ifndef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH custom_crash_callback
#endif
extern "C" void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);

#else
#undef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH(...);
#endif


bool abendIsNetworkOK(void);
void abendShowNetworkHealth(Print& sio);
void abendEnableNetworkMonitor(bool enable);

#endif // ABENDSYSTEMHEALTH_H
