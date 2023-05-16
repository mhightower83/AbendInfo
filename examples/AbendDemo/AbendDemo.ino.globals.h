#ifndef EXCEPTION20_INO_GLOBALS_H
#define EXCEPTION20_INO_GLOBALS_H

/*@create-file:build.opt@

// Removing the optimization for "sibling and tail recursive calls" will clear
// up some gaps in the stack decoder report. Preserves stack frames created at
// each level as you call down to the next.
-fno-optimize-sibling-calls

// No discovery of functions that are pure or constant.
-fno-ipa-pure-const

// Turn off AbendInfo option default is on. Use to disable w/o doing a lot of edits.
// -DOPTION_ABENDINFO=0


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
