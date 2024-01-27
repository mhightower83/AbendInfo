#ifndef EXCEPTION20_INO_GLOBALS_H
#define EXCEPTION20_INO_GLOBALS_H


/*@create-file:build.opt@

-include "/home/mhightow/Arduino/include/ide_globals.h"

// Removing the optimization for "sibling and tail recursive calls" will clear
// up some gaps in the stack decoder report. Preserves stack frames created at
// each level as you call down to the next.
-fno-optimize-sibling-calls


// No discovery of functions that are pure or constant.
-fno-ipa-pure-const


-DUMM_STATS_FULL=1

// Turn off AbendInfo option default is on. Use to disable w/o doing a lot of edits.
// -DABENDINFO_OPTION=0

// Very small reduction in code - removes printing from custom_crash_callback
// -DABENDINFO_POSTMORTEM_EXTRA=0

// While this option is available it is not recommended to change from default.
// Default action, replace all of the Boot ROMs default handlers left in the
// EXCCAUSE table. Uncomment next line to disable replacement.
// -DABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS=0



// To use multiple custom_crash_callback functions we use macros to rename them
// And call them from a single custom_crash_callback function
-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO=abendEvalCrash
-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH=abendNetworkEvalCrash
-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG=backtraceCrashCallback

-DSHARE_PREINIT__DEBUG_ESP_BACKTRACELOG=share_preinit__backtracelog_init

// AbendInfo starts with Network Monitor on by default. Use line below to
// disable.
// -DABENDINFO_NETWORK_MONITOR=0


// -DABENDINFO_IDENTIFY_SDK_PANIC=0

-DABENDINFO_DEBUG=1
*/

/*@create-file:build.opt:debug@
// For this block to work, you must have
// `mkbuildoptglobals.extra_flags={build.debug_port}` in `platform.local.txt`
// Or move contents to the block with the signature "@create-file:build.opt@"


-DABENDINFO_DEBUG=1
*/

#if defined(__cplusplus)
// Defines kept private to .cpp modules
//#pragma message("__cplusplus has been seen")
#endif
#if !defined(__cplusplus) && !defined(__ASSEMBLER__)
// Defines kept private to .c modules
#endif
#if defined(__ASSEMBLER__)
// Defines kept private to assembler modules
#endif
#endif
