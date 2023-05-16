# Abnormal End (abend)

Current status: working - (in the future may have breaking changes)

Some default exception events are only announced if you are running `gdb`. In the absence of `gdb`, this library installs handlers that support collecting crash info that would have otherwise been presented as Hardware WDT or Software WDT Reset.

Grab control before some catastrophic crashes that leave little or no details behind. Capture some details of the crash for later review.

The NONOS SDK does not replace many of the Boot ROM's default exception handlers which only implement break-point loops causing Software or Hardware WDT Resets. Of these, Exception 20 may be the more important. It is the exception you get when calling a NULL function pointer. Or any address that is an invalid instruction memory address. It is unknown to me, how to generate the other unhandled exceptions.

SDK v3.0.5 has 95 Deliberate Infinite Loops which become Hardware WDT or Software WDT Reset depending on whether interrupts are off. 94 of those call `ets_printf` immediately before the Infinite Loop these can be detected and the address reported at reboot. The one that doesn't call `ets_printf` has a break-point before the infinite loop. By wrapping `ets_printf` we can intercept 94 of these failures before they happens.

Forgotten or compiled in break-points can also cause a Software or Hardware WDT Reset. We install a small debug vector stub to forward BPs to the user exception handler as EXCCAUSE 0. These can be identified as BP because epc2 is set with the BP location. Otherwise, epc2 would be NULL.

Some details determined and reported at Postmortem, like divide-by-zero are not
brought forward after restart.

Summary:
 1. Leveraging the SDK's General Exception handler, replace all Boot ROM unhandled exception entries in the Exception Vector table.
   * Alternate option, only install an Exception 20 handler. (My current thinking this alternate should go away.)
 2. Monitor for possible WDT Resets based on deliberate infinite loops.
 3. Avoid HWDT Reset when gdb is _not_ running.
    To capture details, redirect BP handling to exception 0.

This library was constructed to be an easy add-in to a Sketch.
```cpp
#include <AbendInfo.h>
void setup(void) {
  // Install BP and replace Unhandled Exception handling. And updates
  // Arduino's copy of rst_info with corrected/missed details.
  // This could be placed in `preinit()`
  abendHandlerInstall();
  // ...
  Serial.begin(115200);
  // ...
  // Print ESP.getResetInfo() with expanded description.
  abendInfoReport(Serial);  
  // ...
}
void loop(void) {
  //...
}
```

For Sketches that use multiple libraries calling custom_crash_callback, I suggest this convention. To support multiple libraries using custom_crash_callback, add
`-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO=abendEvalCrash` to your `Sketch.ino.globals.h` file. Create a custom_crash_callback function in your Sketch and call SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(...). See end of example `AbendDemoAndBacktrace.ino` and `AbendDemoAndBacktrace.ino.globals.h`. You may need to update libraries that are not using this convention.

Somthing like this in the library's `.h` file sets a default define to `custom_crash_callback`
```cpp
#ifndef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO custom_crash_callback
#endif
```
The main library function then uses the macro to define its version of the custom crash callback. By using global defines we can then easily rename a library's `custom_crash_callback` function to be unique. With all the libraries `custom_crash_callback` functions with unique names, you construct a new inclusive `custom_crash_callback` function in your Sketch and call all the unique callback functions from within. Ideally call them by their macro names.
