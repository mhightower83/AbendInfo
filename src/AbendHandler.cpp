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
 Capture some details of the crash for later review. Grab control before some
 catastrophic crashes that leave little or no details behind.

 By leveraging the SDK's General Exception handler, we can force a stack trace
 for currently unhandled crash events whick appear as Hardware or
 Software WDT Resets.

 Summary:
   1. Monitor for possible Deliberate Infinite Loops, immediately following
      `ets_printf` calls. These often become Soft WDT or HWDT resets. This
      appears to be the method used by the SDK for "panic".
      There are 94 of these in the SDK v3.0.5.
   2. Use the SDK's General Exception handler as a replacement for all the
      remaining Boot ROM unhandled exception vectors.
     * Alternate, minimally replace the Boot ROM Exception 20 handler.
   3. Without gdb, Breakpoint instructions silently turn into Hardware WDT
      resets. For this case, install a mini BP handler that redirects to the
      Exception 0 handler. Letting the SDK's exception process make the BP
      known. Register epc2 holds the address of the BP instruction.

In the absence of gdb, replace the unhandled exception 20 logic which does a
BP with the SDK's General Exception handler.

Redirect debug vector to user with exccause set to 0.

*/
#include "Arduino.h"
#include <user_interface.h>
#include <ets_sys.h> // ets_printf, fp_putc_t
extern "C" void ets_install_putc2(fp_putc_t putc);

#include <gdb_hooks.h>
// #include <xtensa/corebits.h> not in build path :(
#include <umm_malloc/umm_malloc.h>
#include "AbendInfo.h"

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
  The Boot ROM `__divsi3` function handles a divide by 0 by branching to the
  `ill` instruction at address 0x4000dce5. By looking for this address in epc1
  we can separate the divide by zero event from other `ill` instruction events.
*/
constexpr uint32_t divide_by_0_exception = 0x4000dce5u;

// Famous locations in Boot ROM
constexpr uint32_t _xtos_unhandled_exception__bp_address = 0x4000dc4bu;
constexpr uint32_t _xtos_unhandled_interrupt__bp_address = 0x4000dc3cu;

static inline bool is_pc_valid(uint32_t pc) {
    // Instruction address space +size
    //   XCHAL_INSTRAM0_VADDR 0x40000000 +0x100000
    //   XCHAL_INSTRAM1_VADDR 0x40100000 +0x100000
    //   XCHAL_INSTROM0_VADDR 0x40200000 +0x100000
    return pc >= XCHAL_INSTRAM0_VADDR && pc < (XCHAL_INSTROM0_VADDR + XCHAL_INSTROM0_SIZE);
}

#if ABENDINFO_OPTION > 0

#pragma GCC optimize("Os")

AbendInfo abendInfo __attribute__((section(".noinit")));
AbendInfo resetAbendInfo __attribute__((section(".noinit")));

extern "C" {
extern struct rst_info resetInfo;

// Copied from Postmortem
// using numbers different from "REASON_" in user_interface.h (=0..6)
enum rst_reason_sw {
    REASON_SDK_PANIC = 101,
    REASON_USER_STACK_SMASH = 253,
    REASON_USER_SWEXCEPTION_RST = 254
};

#define ALIGN_UP(a, s) ((decltype(a))((((uintptr_t)(a)) + (s-1)) & ~(s-1)))


#if ABENDINFO_IDENTIFY_SDK_PANIC

#if ABENDINFO_HEAP_MONITOR
#define ABENDINFO_EPC1     36
#define ABENDINFO_INTLEVEL 40
#define ABENDINFO_IDX      44
#else
#define ABENDINFO_EPC1     20
#define ABENDINFO_INTLEVEL 24
#define ABENDINFO_IDX      28
#endif

static_assert(offsetof(AbendInfo, intlevel) == ABENDINFO_INTLEVEL);
static_assert(offsetof(AbendInfo, epc1) == ABENDINFO_EPC1);
static_assert(offsetof(AbendInfo, idx) == ABENDINFO_IDX);


static IRAM_ATTR void _gasp_putc(char c) {
    if (sizeof(abendInfo.gasp) - 2 <= abendInfo.idx) return;
    if ('\r' != c && '\n' != c) {
        abendInfo.gasp[abendInfo.idx++] = c;
        abendInfo.gasp[abendInfo.idx] = '\0';
    }
}

/*
  Monitor for calls to `ets_printf(...)` followed by `while(true) { };`.
  Inspect return address for deliberate infinite loop (`loop: j loop`
  // 0x06, 0xFF, 0xFF) When this event is detected, call `ill`.

  The primary goal was to avoid Infinite Loops with Interrupts disabled
  resulting in Hardware WDT Resets. Since this construct appears to be the
  NON-OS SDK's method for implementing a "panic", expand operation to include
  any interrupt level.

  The few ets_printf's I inspected, printed an abreviated module name and line number.
  I assume the SDK sources have a panic macro containing something like:
    #define panic() \
      ets_printf("%s %u\n", moduleId, __LINE__); \
      while (true) { }
*/
asm(
    ".section     .iram.text.infinite_ets_printf,\"ax\",@progbits\n\t"
    ".literal_position\n\t"
    ".literal     .abendInfo, abendInfo\n\t"
    ".literal     .rom_ets_printf, 0x400024cc\n\t"  // Boot ROM ets_printf
    ".align       4\n\t"
    ".global      ets_printf\n\t"
    ".type        ets_printf, @function\n\t"
    "\n"
"ets_printf:\n\t"
    "addi         a1,     a1,     -16\n\t"
    "s32i         a0,     a1,     12\n\t"
    "s32i         a12,    a1,     4\n\t"
    "addi         a12,    a1,     16\n\t"
    "s32i         a12,    a1,     8\n\t"    // Finish Stack Frame for Postmotem

    // While no previous infinite loop detected, clear last gasp index.
    "l32r         a0,     .abendInfo\n\t"
    "l32i         a12,    a0,     " QUOTE( ABENDINFO_EPC1 ) "\n\t"
    "bnez         a12,    ets_printf_continue\n\t"  // Capture 1st event

    "s32i         a12,    a0,     " QUOTE( ABENDINFO_IDX ) "\n\t"  // abendInfo.idx
    "\n"
"ets_printf_continue:\n\t"
    "l32r         a0,     .rom_ets_printf\n\t"
    "callx0       a0\n\t"
    "bnez         a12,    ets_printf_exit\n\t"  // Capture 1st event
    /*
      After ets_printf has done its part. We check if we are returning to an
      infinite loop. If so, the return address location will contain 0xffff06.
      This would have created a Soft WDT reset when INTLEVEL = 0 and a Hardware
      WDT when INTLEVEL != 0. We intercept both.
    */
    // Check if returning to an infinite loop?
    "movi         a3,     ~3\n\t"
    "l32i         a0,     a1,     12\n\t"
    "and          a3,     a3,     a0\n\t"
    "ssa8l        a0\n\t"
    "l32i         a4,     a3,     4\n\t"
    "l32i         a3,     a3,     0\n\t"
    "movi         a6,     0x00ffffff\n\t"
    "src          a3,     a4,     a3\n\t"
    "and          a3,     a3,     a6\n\t"
    "movi         a4,     0x00ffff06\n\t"
    "bne          a3,     a4,     ets_printf_exit\n\t"
    // Return is to an Infinite Loop. Save location for later processing at
    // custom_crash_callback. To ensure a stack trace, force crash with
    // Exception 0.
    "l32r         a5,     .abendInfo\n\t"
    "rsr.ps       a12\n\t"
    "extui        a12,    a12,    0,     4\n\t"
    "s32i         a0,     a5,     " QUOTE( ABENDINFO_EPC1 ) "\n\t"
    "s32i         a12,    a5,     " QUOTE( ABENDINFO_INTLEVEL ) "\n\t"
    "movi         a2,     0\n\t"
    "call0        ets_install_putc2\n\t"
"ets_printf_sdk_panic:\n\t" // Add self explaining label for addr2line to display
    "ill\n\t"
    "\n"
"ets_printf_exit:\n\t"
    "l32i         a0,     a1,     12\n\t"
    "l32i         a12,    a1,     4\n\t"
    "l32i         a1,     a1,     8\n\t"
    "ret\n\t"
    ".size ets_printf, .-ets_printf\n\t"
);
#endif // ABENDINFO_IDENTIFY_SDK_PANIC


static void abendUpdateHeapStats(void) {
    abendInfo.oom = umm_get_oom_count();
    abendInfo.heap = umm_free_heap_size_lw(); // ESP.getFreeHeap();
    abendInfo.heap_min = umm_free_heap_size_min();
}


/*
  Normally called as a weak link replacement of custom_crash_callback at the
  end of Postmortem.

  Print and carry discoveries forward through the restart cycle

  Also, update rst_info so that other custom_crash_callbacks that might follow
  us have access to our discoveries. If this is not desired change the order
  of the calls to custom_crash_callback functions.
*/
extern void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(
    struct rst_info *rst_info,
    uint32_t stack,
    uint32_t stack_end)
{
    (void)stack;
    (void)stack_end;
    abendInfo.uptime = (time_t)(micros64() / 1000000);
    SHOW_PRINTF("\nAbendInfo:\n");
    if (rst_info->reason == REASON_EXCEPTION_RST) {
        if (20u /* EXCCAUSE_INSTR_PROHIBITED */ == rst_info->exccause &&
            !is_pc_valid(rst_info->epc1)) {
            // EXCCAUSE_INSTR_PROHIBITED is commonly the result of calling a
            // NULL or invalid function pointer. The value in both epc1 and
            // excvaddr is the called address, usually 0. For the call case,
            // register a0 will contain the return address. Fixup epc1 with more
            // useful information. excvaddr still contains the called address.
            uint32_t pc;
            __asm__ __volatile__("rsr.excsave1 %[pc]\n\t" : [pc]"=r"(pc):: "memory");
            rst_info->epc1 = pc;
            SHOW_PRINTF("  Possible source of Exception 20 @0x%08x\r\n", rst_info->epc1);
        }
        else if (rst_info->epc2) {
            // When set it is the address of the BP instruction
            SHOW_PRINTF("  Hit breakpoint instruction @0x%08x\r\n", rst_info->epc2);
        }
        else if (0 /* EXCCAUSE_ILLEGAL */ == rst_info->exccause) {
#if ABENDINFO_IDENTIFY_SDK_PANIC
            if (abendInfo.epc1) {
                // abendInfo.epc1 is the address of a Deliberate Infinite Loop
                // Undetected this would be a Hardware WDT reset.
                // Point in the direction of the problem
                rst_info->epc1 = abendInfo.epc1;
                rst_info->reason = REASON_SDK_PANIC;
                SHOW_PRINTF("  SDK Panic: '%s' @0x%08x, INTLEVEL=%u\r\n",
                    abendInfo.gasp, abendInfo.epc1, abendInfo.intlevel);
            } else
#endif
            if (divide_by_0_exception == rst_info->epc1) {
                rst_info->exccause = 6 /* EXCCAUSE_DIVIDE_BY_ZERO */;
                // In place of the detached 'ILL' instruction., redirect attention
                // back to the code that called the ROM divide function.
                uint32_t pc;
                __asm__ __volatile__("rsr.excsave1 %[pc]\n\t" : [pc]"=r"(pc):: "memory");
                rst_info->epc1 = pc;
                // No need to print. Postmortem has already identified this.
                // We just save the info so it can be reported after restart.
            }
        }
    }
#if ABENDINFO_IDENTIFY_SDK_PANIC
    if (0 == abendInfo.epc1 || 0 == abendInfo.idx) {
       abendInfo.gasp[0] = '\0';
    }
#endif
    // Archive net adjustments from Postmortem and above
    abendUpdateHeapStats(); // final update
    if (abendInfo.oom) {
        SHOW_PRINTF("  Heap OOM count: %u\r\n", abendInfo.oom);
    }
    abendInfo.epc1     = rst_info->epc1;
    abendInfo.reason   = rst_info->reason;
    abendInfo.exccause = rst_info->exccause;
    SHOW_PRINTF("\n");
    abendInfo.crc = crc32(&abendInfo, offsetof(struct AbendInfo, crc));
}

extern void _DebugExceptionVector(void);
extern void new_debug_vector(void);
extern void *new_debug_vector_last;

// Replacement _DebugExceptionVector stub
//
// Note EXCCAUSE value is lost for unhandled exceptions. For unhandled
// exceptions and no gdb installed case, it is better to replace the Boot ROM BP
// based unhandled exceptions logic. To preserve EXCCAUSE value, build with
// `-DABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS=1`, current default.
asm(  // _DebugExceptionVector replacement, 16 bytes MAX
    ".section     .text.new_debug_vector,\"ax\",@progbits\n\t"
    ".align       4\n\t"
    ".type        new_debug_vector, @function\n\t"
    "\n"
"new_debug_vector:\n\t"         // Copy to destination _DebugExceptionVector
    "wsr.excsave2   a0\n\t"
    "movi           a0, 0\n\t"
    "wsr.exccause   a0\n\t"     // redirect to exception 0 handler
    "rsr.excsave2   a0\n\t"
    "j              .+53\n\t"   // continue at _UserExceptionVector
"new_debug_vector_last:\n\t"
    ".size new_debug_vector, .-new_debug_vector\n\t"
);


} // extern "C"


#if ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS
#include <esp8266_undocumented.h>
/*
  For exceptions that are directed to one of the general exception vectors
  (UserExceptionVector, KernelExceptionVector, or DoubleExceptionVector) there
  are 64 possible causes. The exception cause register EXCCAUSE has the value.
  The value is in the lower 6 bits (5 ... 0). Other bits in the register are
  reserved and may need to be truncated before use.
*/
constexpr size_t max_num_exccause_values = 64u;

////////////////////////////////////////////////////////////////////////////////
//
static void replace_exception_handler_on_match(
                uint32_t cause,
                _xtos_handler match,
                fn_c_exception_handler_t replacement) {

    _xtos_handler old_wrapper = _xtos_exc_handler_table[cause];
    if (old_wrapper == match || NULL == match) {
        _xtos_set_exception_handler(cause, replacement);
    }
}

// While _xtos_unhandled_exception is in the linker .ld file, it may have been
// overridden. We require the original Boot ROM function address to limit our
// override to the default values still in the table.
const _xtos_handler ROM_xtos_unhandled_exception = (reinterpret_cast<_xtos_handler>(0x4000dc44));

static void install_unhandled_exception_handler(void) {
    // Only replace Exception Table entries still using the orignal Boot ROM
    // _xtos_unhandled_exception handler.
    for (size_t i = 0; i < max_num_exccause_values; i++) {
        replace_exception_handler_on_match(
            i,
            ROM_xtos_unhandled_exception,
            (fn_c_exception_handler_t)_xtos_c_handler_table[0]);
    }
}
#endif  // ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS


// call from setup() or preinit()
/*
  update - Patch Arduino's copy of rst_info
*/
extern "C" void abendHandlerInstall(bool update) {
    const size_t new_debug_vector_sz  = ALIGN_UP((uintptr_t)&new_debug_vector_last - (uintptr_t)new_debug_vector, 4);

    if (! gdb_present()) {
        uint32_t save_ps = xt_rsil(15);
        // Check if putc handler is in IRAM
        if (0x40200000u <= *(uint32_t*)0x3fffdd48u) { // storage for putc1
            // uart.cpp uses a null print function in ICACHE potentially a crash
            // when called from ISR. Replace with NULL.
            ets_install_putc1(NULL);
        }
#if ABENDINFO_IDENTIFY_SDK_PANIC
        // Buffer last gasp from SDK
        // For NON-OS SDK putc2 appears to be unused and available
        ets_install_putc2(_gasp_putc);
#endif
        // Use SDKs General Exception Handler for Exception 20
        #if ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS
        install_unhandled_exception_handler();
        #else
        _xtos_set_exception_handler(20u /* EXCCAUSE_INSTR_PROHIBITED */, _xtos_c_handler_table[0]);
        #endif
        ets_memcpy((void*)_DebugExceptionVector, (void*)new_debug_vector, new_debug_vector_sz);
        // No need to zero exccause, epc1 and excsave1 - the timer tick for the
        // Soft WDT is constantly setting these. Set the rest to zero.
        uint32_t zero = 0;
        asm volatile(
            "wsr.epc2     %[zero]\n\t"
            "wsr.epc3     %[zero]\n\t"
            "wsr.excsave2 %[zero]\n\t"
            "wsr.depc     %[zero]\n\t"
            ::[zero]"r"(zero) :
        );
        xt_wsr_ps(save_ps);
    }

    // Fillin resetInfo with missing details from previous boot crash cycle
    // Update resetAbendInfo with data from previous boot crash cycle
    // init abendInfo for the new boot cycle
    const uint32_t reason = ESP.getResetInfoPtr()->reason;
    bool abendOK = (abendInfo.crc == crc32(&abendInfo, offsetof(struct AbendInfo, crc)));
    //D REASON_DEEP_SLEEP_AWAKE > reason &&
    //D REASON_DEFAULT_RST      < reason &&
    if (abendOK && (REASON_SOFT_RESTART == reason || 100u < abendInfo.reason)) {
        // Added Software Exceptions eg. panic()
        // Don't expect to have valid data after these:
        //   REASON_EXT_SYS_RST
        //   REASON_DEEP_SLEEP_AWAKE
        //   REASON_DEFAULT_RST
        resetAbendInfo = abendInfo;
    } else
    if (abendOK &&
        (REASON_SOFT_WDT_RST  == reason ||
         REASON_EXCEPTION_RST == reason ||
         REASON_WDT_RST       == reason) ) {

        resetAbendInfo = abendInfo;
        if (resetAbendInfo.epc1 && update) {
            // Patch Arduino's copy of rst_info
            resetInfo.epc1     = resetAbendInfo.epc1;
            resetInfo.reason   = resetAbendInfo.reason;
            resetInfo.exccause = resetAbendInfo.exccause;
        }
    } else {
        memset(&resetAbendInfo, 0, sizeof(struct AbendInfo));
    }
    memset(&abendInfo, 0, sizeof(struct AbendInfo));
    #if ABENDINFO_HEAP_MONITOR
    abendInfo.last = millis();
    #endif
}


void abendInfoHeapReport(Print& sio, const char *qualifier, AbendInfo& info) {
#if ABENDINFO_HEAP_MONITOR
    sio.printf_P(PSTR("\r\n%sDRAM Heap Report:\r\n"), qualifier);
    sio.printf_P(PSTR("  %-23S %5u\r\n"), PSTR("OOM count:"), info.oom);
    sio.printf_P(PSTR("  %-23S %5u\r\n"), PSTR("low mark:"), info.heap_min);
    sio.printf_P(PSTR("  %-23S %5u\r\n"), PSTR("free at test interval:"), info.heap);
    if (info.low_count) {                     //12345678901234567890123456
        sio.printf_P(PSTR("  %-23S %5u\r\n"), PSTR("Critically Low:"), info.low_count);
    }
#elif ABENDINFO_OPTION
    if (info.oom) {
        sio.printf_P(PSTR("  DRAM Heap OOM count: %u\r\n"), info.oom);
    }
#endif
}
#endif //#if ABENDINFO_OPTION

static void printTime(Print& sio, PGM_P label, time_t time) {
    char buf[64];
    struct tm *tv = gmtime(&time);
    if (strftime(buf, sizeof(buf), "%T", tv) > 0) {
        sio.printf_P(PSTR("%-23S "), label);
        if (tv->tm_yday)
            sio.printf_P(PSTR("%d day%s"), tv->tm_yday, (tv->tm_yday == 1) ? " " : "s ");

        sio.printf_P(PSTR("%s\r\n  "), buf);
    }

}
void abendInfoReport(Print& sio, bool heap) {
    sio.printf_P(PSTR("\nRestart Report:\n  "));
    if (resetAbendInfo.uptime) {
        printTime(sio, PSTR("Uptime: "), resetAbendInfo.uptime);
        printTime(sio, PSTR("Time since restart: "), (time_t)(micros64() / 1000000));
    }

    [[maybe_unused]] struct rst_info *info = ESP.getResetInfoPtr();
    sio.println(ESP.getResetInfo());

    uint32_t epc1 = info->epc1; //
    uint32_t epc2 = info->epc2; // BP address
    const uint8_t infinite_loop[] = { 0x06, 0xff, 0xff };  // loop: j loop
    if (epc2) {
        /*
          Normally with the Boot ROM's handling of _xtos_unhandled_exception,
          gdb not running, and HWDT Reset, epc2 is never saved to RTC for later
          display after reboot. By installing a simple handler the SDK has a
          chance to commit the epc2 register value to RTC memory.
        */
        sio.printf_P(PSTR("  Hit breakpoint instruction @0x%08x\r\n"), epc2);
    } else
#if ABENDINFO_OPTION > 0
    #if ABENDINFO_IDENTIFY_SDK_PANIC
    if (REASON_SDK_PANIC == resetAbendInfo.reason) {
        sio.printf_P(PSTR("  SDK Panic: '%s' @0x%08x, INTLEVEL=%u\r\n"),
            resetAbendInfo.gasp, resetAbendInfo.epc1, resetAbendInfo.intlevel);
    } else
    #endif
    if (REASON_USER_STACK_SMASH == resetAbendInfo.reason) {
        sio.printf_P(PSTR("  User stack smashed\r\n"));
    } else
    if (REASON_USER_SWEXCEPTION_RST == resetAbendInfo.reason) {
        sio.printf_P(PSTR("  User Software Exception\r\n"));
    } else
    if (is_pc_valid(epc1) && 0 == memcmp_P(infinite_loop, (PGM_VOID_P)epc1, 3u)) {
        sio.printf_P(PSTR("  Deliberate Infinite Loop @0x%08x\r\n"), epc1);
    } else
#else
    if (is_pc_valid(epc1) && 0 == memcmp_P(infinite_loop, (PGM_VOID_P)epc1, 3u)) {
        const uint8_t callx0_a0[] = { 0xc0u, 0x00, 0x00 };
        if (0 == memcmp_P(callx0_a0, (PGM_VOID_P)(epc1 - 3u), 3u)) {
            sio.printf_P(PSTR("  SDK panic @0x%08x\r\n"), epc1);
        } else {
            sio.printf_P(PSTR("  Deliberate Infinite Loop @0x%08x\r\n"), epc1);
        }
    } else
#endif
    if (20u == info->exccause) {
        sio.printf_P(PSTR("  Possible source of Exception 20 @0x%08x\r\n"), epc1);
    }

#if ABENDINFO_OPTION > 0
    if (heap) abendInfoHeapReport(sio, "Restart ", resetAbendInfo);
#endif
}


#if ABENDINFO_HEAP_MONITOR
////////////////////////////////////////////////////////////////////////////////
// These define metrics for detecting chronically low heap
// indicate need for restart after 10 secs of consecutive heap below 4K
constexpr uint32_t kCheckIntervalMs = 1000;
constexpr uint32_t kResetTriggerCount = 60;
constexpr uint32_t kHeapLowTrigger = 4*1024;

/*
  Should be called from the top of `void loop(void) { }`
*/
bool abendIsHeapOK(void) {
    uint32_t now = millis();
    if (now - abendInfo.last > kCheckIntervalMs) {
        abendUpdateHeapStats();
        if (abendInfo.heap < kHeapLowTrigger) {
            abendInfo.low_count++;
        } else {
            abendInfo.low_count = 0;
        }
        abendInfo.last = now;
    }
    // False when heap is chronically low
    return abendInfo.low_count < kResetTriggerCount;
}
#endif  // ABENDINFO_HEAP_MONITOR




////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// scraps
#if 0
bool isCodeAMatch(void *iramPtr, uint32_t codeToMatch);
/*
  Returns true when the instruction pointer content matches the 2 or 3 byte
  instruction supplied by caller.
*/
asm(
    ".section     .iram.text.isCodeAMatch,\"ax\",@progbits\n\t"
    ".literal_position\n\t"
    ".align       4\n\t"
    ".global      isCodeAMatch\n\t"
    ".type        isCodeAMatch, @function\n\t"
    "\n"
"isCodeAMatch:\n\t"
    "movi         a4,     ~3\n\t"
    "and          a4,     a4,     a2\n\t"
    "ssa8l        a2\n\t" // clobbers sar

    "l32i         a5,     a4,     4\n\t"
    "l32i         a4,     a4,     0\n\t"
    "movi         a2,     0x00ffffff\n\t"      // Mask for 3 byte instruction
    "src          a4,     a5,     a4\n\t"

    "bbci         a3,     3,      isCodeAMatch_3byte\n\t" // bit in mask 0x08 2 byte Instruction
    "srli         a2,     a2,     8\n\t"      // adjust mask for two byte instruction
    "\n"
"isCodeAMatch_3byte:\n\t"
    "and          a4,     a4,     a2\n\t"

    "sub          a4,     a4,     a3\n\t"
    "addi         a2,     a4,     1\n\t"
    "bnei         a2,     1,      isCodeAMatch_exit\n\t"
    "movi         a2,     0\n\t"

"isCodeAMatch_exit:\n\t"
    "ret\n\t"
    ".size isCodeAMatch, . - isCodeAMatch\n\t"
    );
#endif
#if 0
bool isCodeAMatch(void *iramPtr, uint32_t codeToMatch, uint32_t codeMask);
/*
  Returns true when the instruction pointer content matches instruction/mask
  supplied by caller
*/
asm(
    ".section     .iram.text.isCodeAMatch,\"ax\",@progbits\n\t"
    ".literal_position\n\t"
    ".align       4\n\t"
    ".global      isCodeAMatch\n\t"
    ".type        isCodeAMatch, @function\n\t"
    "\n"
"isCodeAMatch:\n\t"
    "movi         a5,     ~3\n\t"
    "and          a5,     a5,     a2\n\t"
    "ssa8l        a2\n\t" // clobbers sar
    "l32i         a6,     a5,     4\n\t"
    "l32i         a5,     a5,     0\n\t"
    "src          a5,     a6,     a5\n\t"

    "and          a5,     a5,     a4\n\t"
    "and          a3,     a3,     a4\n\t"
    "sub          a5,     a5,     a3\n\t"
    "addi         a2,     a5,     1\n\t"
    "movi         a3,    0\n\t"
    "movnez       a2,    a3,      a5\n\t"
    "ret\n\t"
    ".size isCodeAMatch, . - isCodeAMatch\n\t"
    );
#endif

#if 0
// Not using this - keeping here so I don't have to search for it again.

// Replaces SDK's empty weak link version. system_restart_hook is called just
// before icache goes offline and reboot.
// ref: https://github.com/pvvx/esp8266web/blob/0de8a993c04ea6c8dfdef391dd1a1e7fec6466da/info/libs/main/user_interface.c#L269
extern "C" void system_restart_hook([[maybe_unused]] struct rst_info *rst_info) {
    // // Write our modifications back so they are available at reboot through
    // // This is questionable technique
    // system_rtc_mem_write(0, &resetInfo, sizeof(struct rst_info));
}
#endif
#if 0
// Alternate version with more "C" code than ASM
//  72 bytes of IRAM larger than the other method.
static IRAM_ATTR uint8_t _get_uint8(const void *p8) {
    void *v32 = (void *)((uintptr_t)p8 & ~(uintptr_t)3u);
    uint32_t val;
    __builtin_memcpy(&val, v32, sizeof(uint32_t));  // requires -Os
    asm volatile ("" :"+r"(val) ::"memory"); // inject 32-bit dependency
    uint32_t pos = ((uintptr_t)p8 & 3u) * 8u;
    val >>= pos;
    return (uint8_t)val;
}

static IRAM_ATTR bool _check_infinite_loop(const void* pc) __attribute__((used));
static IRAM_ATTR bool _check_infinite_loop(const void* pc) {
    const uint8_t* p = (const uint8_t*)pc;
    if (_get_uint8(&p[0]) == 0x06u &&
        _get_uint8(&p[1]) == 0xFFu &&
        _get_uint8(&p[2]) == 0xFFu)  {
        abendInfo.epc1 = (uint32_t)pc;
        return true;
    }
    return false;
}

asm(
    ".section     .iram.text.ets_printf,\"ax\",@progbits\n\t"
    ".literal_position\n\t"
    ".literal     .abendInfo, abendInfo\n\t"
    ".align       4\n\t"
    ".global      ets_printf\n\t"
    ".type        ets_printf, @function\n\t"
    "\n"
"ets_printf:\n\t"
    "addi         a1,     a1,     -16\n\t"
    "s32i         a0,     a1,     12\n\t"
    "addi         a0,     a1,     16\n\t"
    "s32i         a0,     a1,     8\n\t"
    // While no previous infinite loop detected, clear last gasp index.
    "s32i         a2,     a1,     4\n\t"
    "l32r         a0,     .abendInfo\n\t"
    "l32i         a2,     a0,     " QUOTE( ABENDINFO_EPC1 ) "\n\t"
    "bnez         a2,     ets_printf_continue\n\t"
    "s32i         a2,     a0,     " QUOTE( ABENDINFO_IDX ) "\n\t"  // abendInfo.idx
    "\n"
"ets_printf_continue:\n\t"
    "movi         a0,     0x400024cc\n\t" // Boot ROM ets_printf
    "l32i         a2,     a1,     4\n\t"
    "callx0       a0\n\t"
    "s32i         a2,     a1,     0\n\t"
    "movi         a3, _check_infinite_loop\n\t"
    "l32i         a2,     a1,     12\n\t"
    "callx0       a3\n\t"
    "bnez         a2,     ets_printf_ill\n\t"
    "l32i         a2,     a1,     0\n\t"
    "l32i         a0,     a1,     12\n\t"
    "l32i         a1,     a1,     8\n\t"
    "ret\n\t"
    "\n"
"ets_printf_ill:\n\t"
    "movi         a2,     0\n\t"
    "call0        ets_install_putc2\n\t"
    "ill\n\t"
  ".size ets_printf, .-ets_printf\n\t"
);
#endif
