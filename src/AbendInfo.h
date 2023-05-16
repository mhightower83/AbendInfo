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
#ifndef ABENDINFO_H_
#define ABENDINFO_H_

#ifndef OPTION_ABENDINFO
#define OPTION_ABENDINFO 1
#endif

#if OPTION_ABENDINFO
#ifndef OPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS
#define OPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS 1
#endif

/*
  To support multiple libraries using custom_crash_callback, add
  `-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO=abendEvalCrash` to your
  Sketch.globals.h file. Create a custom_crash_callback function in your
  Sketch and call HARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(...). See end of
  example `AbendDemo.ino` and `AbendDemo.globals.h`.
*/
#ifndef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO custom_crash_callback
#endif

struct AbendInfo {
    uint32_t epc1;  // Needs to be first for ASM to work
    uint32_t reason;
    uint32_t exccause;
    // uint32_t crc;  // maybe TODO
};
extern AbendInfo abendInfo;
extern AbendInfo resetAbendInfo;

void abendHandlerInstall(void);
void abendInfoReport(Print& sio);
// extern "C" void abendEvalCrash(struct rst_info *rst_info);

#else
#undef OPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS
#define OPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS 0

static inline void abendHandlerInstall(void) {}
void abendInfoReport(Print& sio);
#endif

#endif
