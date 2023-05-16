#ifndef EXCEPTION20_INO_GLOBALS_H
#define EXCEPTION20_INO_GLOBALS_H

/*@create-file:build.opt@

// Removing the optimization for "sibling and tail recursive calls" will clear
// up some gaps in the stack decoder report. Preserves stack frames created at
// each level as you call down to the next.
-fno-optimize-sibling-calls


// No discovery of functions that are pure or constant.
-fno-ipa-pure-const

//
//
// -DDEMO_WIFI=1

-DUMM_STATS_FULL=1

// Turn off AbendInfo option default is on. Use to disable w/o doing a lot of edits.
// -DOPTION_ABENDINFO=0


// While this option is available it is not recommended to change from default.
// Default action, replace all of the Boot ROMs default handlers left in the
// EXCCAUSE table. Uncomment next line to disable replacement.
// -DOPTION_REPLACE_ALL_DEFAULT_EXC_HANDLERS=0


// To use multiple custom_crash_callback functions we use macros to rename them
// And call them from a single custom_crash_callback function
-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO=abendEvalCrash
-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG=backtraceCrashCallback


-DSHARE_PREINIT__DEBUG_ESP_BACKTRACELOG=share_preinit__backtracelog_init
*/

/*@create-file:build.opt:debug@
// For this block to work, you must have
// `mkbuildoptglobals.extra_flags={build.debug_port}` in `platform.local.txt`
// Or move contents to the block with the signature "@create-file:build.opt@"



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
