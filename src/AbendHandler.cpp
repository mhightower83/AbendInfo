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
 * Capture some details of the crash for later review. Grab control before some
 * catastrophic crashes that leave little or no details behind.
 *
 * Summary:
 *   1. Monitor for possible HWDT Reset based infinite loop.
 *   2. Install Exception 20 handler. Use SDK's General Exception handler
 *   3. Avoid HWDT Reset when gdb is not running.
 *      Redirect BP handling to exception 0
 *
 * The goal here is to avoid Infinite Loops with Interrupts disabled resulting
 * in HWDT. Inspect return address for deliberate infinite loop (`loop: j loop`
 * // 0x06, 0xFF, 0xFF) When a pending HWDT of this type is detected call `ill`
 *
 * In the absence of gdb, replace the unhandled exception 20 logic which does a
 * BP with the SDK's General Exception handler.
 *
 * Redirect debug vector to user with exccause set to 0.
 */
#include "Arduino.h"
#include <user_interface.h>
#include <ets_sys.h> // ets_printf
#include <gdb_hooks.h>
// #include <xtensa/corebits.h> not in build path :(
#include <umm_malloc/umm_malloc.h>
#include "AbendInfo.h"

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

#if OPTION_ABENDINFO > 0

#pragma GCC optimize("Os")

AbendInfo abendInfo __attribute__((section(".noinit")));
AbendInfo resetAbendInfo __attribute__((section(".noinit")));
static_assert(offsetof(AbendInfo, epc1) == 0);


extern "C" {
extern struct rst_info resetInfo;

// Copied from Postmortem
// using numbers different from "REASON_" in user_interface.h (=0..6)
enum rst_reason_sw {
    REASON_USER_STACK_SMASH = 253,
    REASON_USER_SWEXCEPTION_RST = 254
};

#define ALIGN_UP(a, s) ((decltype(a))((((uintptr_t)(a)) + (s-1)) & ~(s-1)))

extern "C" {
#if 1
asm(
    ".section     .iram.text.infinite_ets_printf,\"ax\",@progbits\n\t"
    ".literal_position\n\t"
    ".literal     .abendInfo_epc1, abendInfo\n\t"
    ".align       4\n\t"
    ".global      ets_printf\n\t"
    ".type        ets_printf, @function\n\t"
    "\n"
"ets_printf:\n\t"
    "addi         a1,     a1,     -16\n\t"
    "s32i         a0,     a1,     12\n\t"
    "movi         a0,     0x400024cc\n\t" // Boot ROM ets_printf
    "callx0       a0\n\t"

    /*
      After ets_printf has done its part. We check if we are returning to an
      infinite loop. If so, the return address location will contain ffff06
      Only, handle case when interrupts are disabled - this is a special case
      that results in HWDT Reset.
    */
    // Are interrupts enabled?
    "rsr.ps       a3\n\t"
    "addi         a5,     a1,     16\n\t"
    "extui        a4,     a3,     0,    4\n\t"
    "s32i         a5,     a1,     8\n\t"
    "beqz         a4,     ets_printf_exit\n\t"

    // Interrupts are disabled.
    // Are we returning to an infinite loop?
    "movi         a3,     ~3\n\t"
    "l32i         a0,     a1,     12\n\t"
    "rsr.sar      a5\n\t"
    "and          a3,     a3,     a0\n\t"
    "ssa8l        a0\n\t" // clobbers sar
    "l32i         a4,     a3,     4\n\t"
    "l32i         a3,     a3,     0\n\t"
    "movi         a6,     0x00ffffff\n\t"
    "src          a3,     a4,     a3\n\t"
    "and          a3,     a3,     a6\n\t"
    "movi         a4,     0x00ffff06\n\t"
    "wsr.sar      a5\n\t"
    "bne          a3,     a4,     ets_printf_exit\n\t"

    // Return is to an Infinite Loop. Save location location for further
    // processing at custom_crash_callback. And, crash with exception 0.
    "l32r         a5,     .abendInfo_epc1\n\t"
    "s32i         a0,     a5,     0\n\t"
    "\n"
    "ill\n\t"
    "\n"
"ets_printf_exit:\n\t"
    "l32i         a0,     a1,     12\n\t"
    "l32i         a1,     a1,     8\n\t"
    "ret\n\t"
    ".size ets_printf, .-ets_printf\n\t"
);

#else
// Alternate version with more "C" code than ASM
static IRAM_ATTR uint8_t _get_uint8(const void *p8) {
    void *v32 = (void *)((uintptr_t)p8 & ~(uintptr_t)3u);
    uint32_t val;
    __builtin_memcpy(&val, v32, sizeof(uint32_t));  // requires -Os
    asm volatile ("" :"+r"(val) ::"memory"); // inject 32-bit dependency
    uint32_t pos = ((uintptr_t)p8 & 3u) * 8u;
    val >>= pos;
    return (uint8_t)val;
}
static IRAM_ATTR void _check_infinite_loop(const void* pc, uint32_t ps) __attribute__((used));
static IRAM_ATTR void _check_infinite_loop(const void* pc, uint32_t ps) {
    const uint8_t* p = (const uint8_t*)pc;
    if (0 != (ps & 0x0Fu) &&
        _get_uint8(&p[0]) == 0x06u &&
        _get_uint8(&p[1]) == 0xFFu &&
        _get_uint8(&p[2]) == 0xFFu)  {
        abendInfo.epc1 = pc;
        ets_uart_printf("\nDetected deliberate infinite loop w/interrupts off @IP:SP %08x:%08x\n", (uint32_t)pc, sp);
        return false;
    }
    return true;
}

asm(
    ".section     .iram.text.ets_printf,\"ax\",@progbits\n\t"
    ".literal_position\n\t"
    ".align       4\n\t"
    ".global      ets_printf\n\t"
    ".type        ets_printf, @function\n\t"
    "\n"
"ets_printf:\n\t"
    "addi         a1,     a1,     -16\n\t"
    "s32i         a0,     a1,     12\n\t"
    "addi         a0,     a1,     16\n\t"
    "s32i         a0,     a1,     8\n\t"
    "movi         a0,     0x400024cc\n\t" // Boot ROM ets_printf
    "callx0       a0\n\t"
    "s32i         a2,     a1,     0\n\t"
    "l32i         a2,     a1,     12\n\t"
    "movi         a4, _check_infinite_loop\n\t"
    "rsr.ps       a3\n\t"
    "callx0       a4\n\t"
    "bnez         a2,     ets_printf_exit\n\t"
    "\n"
    "ill\n\t"
    "\n"
"ets_printf_exit:\n\t"
    "l32i         a2,     a1,     0\n\t"
    "l32i         a0,     a1,     12\n\t"
    "l32i         a1,     a1,     8\n\t"
    "ret\n\t"
  ".size ets_printf, .-ets_printf\n\t"
);
#endif

};

/*
  Carry discoveries forward through restart cycle

  Normally called as a weak link replacement of custom_crash_callback at the
  end of Postmortem.
*/
extern void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(
    struct rst_info *rst_info,
    uint32_t stack,
    uint32_t stack_end)
{
    (void)stack;
    (void)stack_end;
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
        }
        else if (rst_info->epc2) {
            // Leave - when set it is the address of the BP instruction
        }
        else if (0 == rst_info->exccause && abendInfo.epc1) {
            // abendInfo.epc1 is the address of a Deliberate Infinite Loop
            // Undetected this would be a Hardware WDT reset.
            // Point in the direction of the problem
            rst_info->epc1 = abendInfo.epc1;
            // Make it look like a typical Hardware WDT Reset
            rst_info->reason = REASON_WDT_RST;
            rst_info->exccause = 4 /* EXCCAUSE_LEVEL1_INTERRUPT */;
        }
        else if (0 /* EXCCAUSE_ILLEGAL */ == rst_info->exccause &&
                 divide_by_0_exception == rst_info->epc1) {
            rst_info->exccause = 6 /* EXCCAUSE_DIVIDE_BY_ZERO */;
            // In place of the detached 'ILL' instruction., redirect attention
            // back to the code that called the ROM divide function.
            uint32_t pc;
            __asm__ __volatile__("rsr.excsave1 %[pc]\n\t" : [pc]"=r"(pc):: "memory");
            rst_info->epc1 = pc;
        }
    }
    // Archive net adjustments from Postmortem and above
    abendInfo.epc1     = rst_info->epc1;
    abendInfo.reason   = rst_info->reason;
    abendInfo.exccause = rst_info->exccause;
}

// Replaces SDK's empty weak link version. system_restart_hook is called just
// before icache goes offline and reboot.
// ref: https://github.com/pvvx/esp8266web/blob/0de8a993c04ea6c8dfdef391dd1a1e7fec6466da/info/libs/main/user_interface.c#L269
// extern void system_restart_hook([[maybe_unused]] struct rst_info *rst_info) {
//     // Write our modifications back so they are available at reboot through
//     system_rtc_mem_write(0, &resetInfo, sizeof(struct rst_info));
// }

extern void _DebugExceptionVector(void);
extern void new_debug_vector(void);
extern void *new_debug_vector_last;

// Replacement _DebugExceptionVector stub
//
// Note EXCCAUSE value is lost for unhandled exceptions. For unhandled
// exceptions and no gdb installed case, it is better to replace the Boot ROM BP
// based unhandled exceptions logic. To preserve EXCCAUSE value, build with
// `-DOPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS=1`, current default.
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

const size_t new_debug_vector_sz  = ALIGN_UP((uintptr_t)&new_debug_vector_last - (uintptr_t)new_debug_vector, 4);
}; // extern "C"

#if OPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS
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
#endif

// call from setup()
void abendHandlerInstall(void) {
    if (!gdb_present()) {
        uint32_t save_ps = xt_rsil(15);
        // Use SDKs General Exception Handler for Exception 20
        #if OPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS
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
    uint32_t reason = ESP.getResetInfoPtr()->reason;
    if (REASON_SOFT_WDT_RST  == reason  ||
        REASON_EXCEPTION_RST == reason  ||
        REASON_WDT_RST       == reason)
    {
        resetAbendInfo = abendInfo;
        if (resetAbendInfo.epc1) {
            // Patch Arduino's copy of rst_info
            resetInfo.epc1     = resetAbendInfo.epc1;
            resetInfo.reason   = resetAbendInfo.reason;
            resetInfo.exccause = resetAbendInfo.exccause;
        }
    } else if (abendInfo.reason > 100u) { // Added Software Exceptions eg. panic()
        resetAbendInfo = abendInfo;
    } else {
        memset(&resetAbendInfo, 0, sizeof(AbendInfo));
    }
    memset(&abendInfo, 0, sizeof(AbendInfo));
}
#endif //#if OPTION_ABENDINFO

void abendInfoReport(Print& sio) {
    sio.printf_P(PSTR("\nRestart Report:\n  "));
    [[maybe_unused]] struct rst_info *info = ESP.getResetInfoPtr();
    sio.println(ESP.getResetInfo());

    uint32_t epc1 = info->epc1; //
    uint32_t epc2 = info->epc2; // BP address
    const char infinite_loop[] = { 0x06, 0xff, 0xff };  // loop: j loop
    if (epc2) {
        sio.printf_P(PSTR("  Hit breakpoint instruction @0x%08x\r\n"), epc2);
        /*
          Due to the Boot ROMs handling of _xtos_unhandled_exception, gbd not
          running, and HWDT Reset, epc2 is never saved to RTC for later display
          after reboot. With epc2 value left at of zero this path will never be
          true.

          By building with `-DOPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS=1`, these
          will be processed through EXCCAUSE 0 handler which will save EPC2, etc.

        if (_xtos_unhandled_exception__bp_address == epc2) {
            sio.printf_P(PSTR("  XTOS Unhandled exception - BP"));
        } else
        if (_xtos_unhandled_interrupt__bp_address == epc2) {
            // TODO how do we findout which interrupt?
            sio.printf_P(PSTR("  XTOS Unhandled interrupt - BP"));
        } else {
            sio.printf_P(PSTR("  Hit breakpoint instruction @0x%08x\r\n"), epc2);
        }
        */
    }
    else if (is_pc_valid(epc1) && 0 == memcmp_P(infinite_loop, (PGM_VOID_P)epc1, 3u)) {
        sio.printf_P(PSTR("  Deliberate infinite loop detected @0x%08x\r\n"), epc1);
    }
    #if OPTION_ABENDINFO > 0
    else if (REASON_USER_STACK_SMASH == resetAbendInfo.reason) {
        sio.printf_P(PSTR("  User stack smashed\r\n"));
    }
    else if (REASON_USER_SWEXCEPTION_RST == resetAbendInfo.reason) {
        sio.printf_P(PSTR("  User Software Exception\r\n"));
    }
    #endif
}
