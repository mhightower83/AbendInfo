# Abnormal End (abend) Information
Current status: working - (in the future may have breaking changes)

## Goals
1. I want to be able to differentiate SDK Crashes from mine. As it now stands that is not possible. The SDK has a nasty way of handling unrecoverable events, it fails with a Software or Hardware WDT Reset.
2. I need to detect that the WiFi interface is hung and not sending and/or receiving packets.
   * WIP, Network Health Monitor component.
3. Extend new detected crash information forward to the next boot cycle.


## Overview
Intercept some crash events that result in Hardware or Software WDT Resets. Cache the results in no-init DRAM for later display on request after reboot. By leveraging the SDK's General Exception handler, we can force a Postmortem crash dump and allow `custom_crash_callback` supporting libraries like [EspSaveCrash](https://github.com/krzychb/EspSaveCrash) or [BacktraceLog](https://github.com/mhightower83/BacktraceLog) to run.
* **TODO: I think I forgot to do this.** I need to do a PR for EspSaveCrash or document changes that facilitate the sharing of `custom_crash_callback`. Add an example to use it.

#### Summary:
  1. Monitor for possible Deliberate Infinite Loops, immediately
     following `ets_printf` calls. These often become Soft WDT or Hardware WDT resets. This appears to be the method used by the SDK for "panic". There are 94 of these in SDK v3.0.5.
  2. Use the SDK's General Exception handler as a replacement for all the
     remaining Boot ROM unhandled exception vectors.
    * Alternate option, minimally replace the Boot ROM Exception 20 handler.
  3. Without gdb, Breakpoint instructions silently turn into Hardware WDT
     resets. For this case, install a mini BP handler that redirects to the
     Exception 0 handler. Letting the SDK's exception process make the BP
     known. Register epc2 holds the address of the BP instruction.
  4. WIP - Network Health Monitor component - The goal for this extension was
     to get a handle on why the WiFi stops working. So far no success.
     (My suspension is a hardware design flaw, resulting in a WiFi hang, which
     would not be unusual for a complex network chip. Without an errata sheet,
     we will never know. And, if we had access to one, we could never speak of
     it because of NDAs.)
     * Now checks if we can receive packets, by sending ARPs to the gateway and
     looks at the size of the ARP table. Also verifies we still have a local IP
     Address. A failure is assumed after 20 minutes and an empty ARP table. Uses
     an unstructured method to gain access to lwIP's ARP table.
     Also added, logic to monitor WiFi Buffer Pools.

## Details
Some default exception events are only announced if you are running `gdb`. In the absence of `gdb`, they are seen as Hardware WDT Reset or Software WDT Reset. To gain access to a stack trace when `gdb` is not installed, this library installs handlers that convert those events to an `ill` instruction, EXCCAUSE 0, crash. Look at the stack trace before the `ill` instruction was encountered.

The Boot ROM's default exception handlers implement breakpoint loops, which in the absence of `gdb` result in Software or Hardware WDT Resets. The NON-OS SDK does not replace many of the Boot ROM's default exception handlers. Of these, exception cause 20 may be the most important. It is the exception you get when calling a NULL function pointer. Or any address that is an invalid instruction memory address. It is unknown to me, how to generate the other unhandled exceptions.

SDK v3.0.5 has 95 Deliberate Infinite Loops which become Hardware WDT or Software WDT Reset depending on whether interrupts are _off_ or _on_ respectfully. 94 of the 95 call `ets_printf` immediately before the Infinite Loop, these can be detected and their address reported at reboot. The 1 of 95 that doesn't call `ets_printf` has a breakpoint before the infinite loop. By wrapping `ets_printf` and monitoring the return address instruction, we can intercept 94 of these failures before they happen. The first 63 characters of that last `printf` are stored for later.

Forgotten or compiled in breakpoints can also cause a Software or Hardware WDT Reset. We install a small debug vector stub to forward BPs to the user exception handler as EXCCAUSE 0. When viewing a stack dump if register `epc2` is non-zero, ignore register `epc1` it points at the `ill` instruction we used to force a stack trace. Use the address in register `epc2` to find the address of the Breakpoint instruction. Note on the ESP8266, Breakpoints are ignored when INTLEVEL is at or above 2.

Some details determined and reported at Postmortem, like divide-by-zero were not brought forward after restart. Those are now saved and the `resetInfo` structure refereced by `ESP.getResetInfo()` can be updated by a call to `abendHandlerInstall(true)` at restart. If you do not want `resetInfo` updated, call `abendHandlerInstall(false)` or `abendHandlerInstall()` at restart.


Example of library added to a Sketch.
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
  // Print ESP.getResetInfo() with expanded results.
  abendInfoReport(Serial);  
  // ...
}
void loop(void) {
  //...
}
```

### How to handle multiple `custom_crash_callback`
For Sketches that use multiple libraries calling `custom_crash_callback`, I suggest this convention. Add
`-DSHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO=abendEvalCrash` to your `Sketch.ino.globals.h` file. Create a custom_crash_callback function in your Sketch and call SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(...). See end of example `AbendDemoAndBacktrace.ino` and `AbendDemoAndBacktrace.ino.globals.h`. You may need to update libraries that are not using this convention.

Somthing like this in the library's `.h` file sets a default define to `custom_crash_callback`
```cpp
#ifndef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_SOME_LIBRARY
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_SOME_LIBRARY custom_crash_callback
#endif
```
The main library's `custom_crash_callback` function name is changed to `SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_SOME_LIBRARY`. By using global defines we can then easily rename a library's `custom_crash_callback` function to be unique. With all the libraries `custom_crash_callback` functions with unique names, you construct a new inclusive `custom_crash_callback` function in your Sketch and call all the unique callback functions from within. Ideally calling them by their macro names.

## Build Customization Options
For the Arduino IDE build platform, all options listed can go in your [`<sketch name>.ino.globals.h`](https://arduino-esp8266.readthedocs.io/en/latest/faq/a06-global-build-options.html?highlight=build.opt#how-to-specify-global-build-defines-and-options) file.
Otherwise, use the method appropriate for your build platform of choice.

Options `ABENDINFO_POSTMORTEM_EXTRA`, `ABENDINFO_IDENTIFY_SDK_PANIC`, and  `ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS` are on by default.
 To become available, option `ABENDINFO_HEAP_MONITOR` requires `-DUMM_STATS_FULL=1`.

### `ABENDINFO_POSTMORTEM_EXTRA`
Defaults to enabled, 1. Used to provide additional info after the Postmortem report at custom crash callback. To disable, set ` -DABENDINFO_POSTMORTEM_EXTRA=0` in `Sketch.ino.globals.h file`. Very small reduction in code - removes printing from custom_crash_callback.

For the case of HWDT Deliberate Infinite Loop, `epc1` in the Postmortem report is confusing. It points to the `ill` instruction used to generate the exception not the cause. This option, brings attention to the Infinite Loop. This is already addressed at reboot.

### `ABENDINFO_IDENTIFY_SDK_PANIC`
Defaults to enabled, 1. Adds a wrapper to `ets_printf` calls to detect if the call is part of an SDK panic. These calls are followed by an Infinite Loop. This option will identify SDK panic events and save the short message printed. The few messages I inspected closely appear to be an abbreviated module name followed by a line number. If this pattern holds, this could be used recognize repeated crash locations event if the address changes when recompiled.

### `ABENDINFO_GASP_SIZE`
Defaults to 64. Use to adjust the size of the last gasp buffer. This buffer stores the `ets_printf` message that occurs before the SDK crashes with an Infinite Loop. Most SDK debug messages are short.

### `ABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS`
Defaults to enabled, 1. Replace all of the Boot ROMs default handlers remaining in the EXCCAUSE table. To disable, set `-DABENDINFO_REPLACE_ALL_DEFAULT_EXC_HANDLERS=0` in `Sketch.ino.globals.h file`.
When disabled, only the handler for EXCCAUSE 20 is replaced.

### `ABENDINFO_HEAP_MONITOR`
Requires `-DUMM_STATS_FULL=1` build flag. When `UMM_STATS_FULL` is enabled `ABENDINFO_HEAP_MONITOR` is automaticly enabled. If you want it to always be off set `-DABENDINFO_HEAP_MONITOR=0` in you build.

Call `abendIsHeapOK()` from the top of `loop()` to monitor for shrinking heap. Returns false when the Heap falls below 4K for an extended period. After restart the previous statistics are reported with a call to `abendInfoReport`.

### `SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO`
Defaults to `custom_crash_callback`. Use this option to set an alternative function name used within `AbendHandler.cpp`. When used, a suggested alternative name is `abendEvalCrash`. This macro supports calling the `AbendHandler.cpp`'s `custom_crash_callback` function from another `custom_crash_callback` function. When used, a suggested practice is to use the macro name `SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(...)` in those calls.
