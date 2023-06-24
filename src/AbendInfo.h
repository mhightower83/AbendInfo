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

#ifndef ABENDINFO_OPTION
#define ABENDINFO_OPTION 1
#endif

#if ABENDINFO_OPTION

#ifndef ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS
#define ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS 1
#endif

// Adds additional printing of our discoveries after the Postmortem report.
// Set to zero for a very small reduction in code.
#ifndef ABENDINFO_POSTMORTEM_EXTRA
#define ABENDINFO_POSTMORTEM_EXTRA 1
#endif

#ifndef ABENDINFO_IDENTIFY_SDK_PANIC
#define ABENDINFO_IDENTIFY_SDK_PANIC 1
#if !defined(ABENDINFO_GASP_SIZE)
#define ABENDINFO_GASP_SIZE 64
#endif

#else
#define ABENDINFO_IDENTIFY_SDK_PANIC 0
#undef ABENDINFO_GASP_SIZE
#define ABENDINFO_GASP_SIZE 0
#endif

// #if !defined(ABENDINFO_GASP_SIZE)
// #define ABENDINFO_GASP_SIZE 64
// #endif

#ifndef ABENDINFO_HEAP_MONITOR
#ifdef UMM_STATS_FULL
#define ABENDINFO_HEAP_MONITOR 1
#else
#define ABENDINFO_HEAP_MONITOR 0
#endif
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
extern "C" void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);


struct AbendInfo {
    time_t uptime;
    uint32_t reason;
    uint32_t exccause;
    uint32_t oom;
#if ABENDINFO_HEAP_MONITOR
    size_t heap;
    size_t heap_min;
    size_t low_count;
    uint32_t last;
#endif
    uint32_t epc1;
#if ABENDINFO_IDENTIFY_SDK_PANIC
    uint32_t intlevel;
    size_t   idx;
    char     gasp[ABENDINFO_GASP_SIZE];  // Buffer last ets_printf message - last gasp
#endif
    uint32_t crc;   // Must be last element
};
extern AbendInfo abendInfo;
extern AbendInfo resetAbendInfo;
extern "C" void abendHandlerInstall(bool update=false);
void abendInfoHeapReport(Print& sio, const char *qualifier="", AbendInfo& info=abendInfo);

#else  // ABENDINFO_OPTION
#undef ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS
#define ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS 0

#undef ABENDINFO_POSTMORTEM_EXTRA
#define ABENDINFO_POSTMORTEM_EXTRA 0

#undef ABENDINFO_HEAP_MONITOR
#define ABENDINFO_HEAP_MONITOR 0

#undef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(...);

#define abendInfoHeapReport(...)
static inline void abendHandlerInstall([[maybe_unused]] bool update=false; ) {}
#endif   // ABENDINFO_OPTION

#if ABENDINFO_HEAP_MONITOR
bool abendIsHeapOK(void);
#else
static inline bool abendIsHeapOK(void) { return true; }
#endif

// A reduced report is made available
void abendInfoReport(Print& sio, bool heap=true);

#endif   // #ifndef ABENDINFO_H_
