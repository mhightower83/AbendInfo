#ifndef ABENDDEMO_INO_GLOBALS_H
#define ABENDDEMO_INO_GLOBALS_H

/*@create-file:build.opt@

// Removing the optimization for "sibling and tail recursive calls" will clear
// up some gaps in the stack decoder report. Preserves stack frames created at
// each level as you call down to the next.
-fno-optimize-sibling-calls

// No discovery of functions that are pure or constant.
-fno-ipa-pure-const

// Turn off AbendInfo option default is on. Use to disable w/o doing a lot of edits.
// -DABENDINFO_OPTION=0

// Very small reduction in code - removes printing from custom_crash_callback
// -DABENDINFO_POSTMORTEM_EXTRA=0


-DUMM_STATS_FULL=1
*/

/*@create-file:build.opt:debug@
// For this block to work, you must have
// `mkbuildoptglobals.extra_flags={build.debug_port}` in `platform.local.txt`
// Or move contents to the block with the signature "@create-file:build.opt@"

-Og
-fno-ipa-pure-const

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
