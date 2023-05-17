/*
   AbendAndBacktrace.ino - An example to generate some of the various ways a
   sketch could crash and show case the AbendInfo library and the BacktraceLog
   libraries sharing `custom_crash_callback`.

   Updated to show sharing preinit() as well.

   For how to use, see calls to `abendHandlerInstall()` and `abendInfoReport()`
   in `setup()` and `custom_crash_callback(...)` definition.
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <AbendInfo.h>
#include <BacktraceLog.h>
BacktraceLog backtraceLog;

void processKey(Print& out, int hotKey);


void setup(void) {
  // Install BP handler and replace Unhandled Exception handling. And updates
  // Arduino's copy of rst_info with corrected/missed details.
  // As an alternative, this call could be moved into `preinit()`.
  // abendHandlerInstall();

  Serial.begin(115200);
  delay(200);    // This delay helps when using the 'Modified Serial monitor' otherwise it is not needed.
  Serial.printf_P(PSTR("\r\n\r\nDemo Sketch of AbendInfo and BacktraceLog ...\r\n\r\n"));

  // Report on previous crash info and state
  // Print ESP.getResetInfo() with expanded description.
  abendInfoReport(Serial);

  backtraceLog.report(Serial);

  Serial.println();
  processKey(Serial, '?');
}


void loop(void) {
  abendIsHeapOK();        // optional
  if (Serial.available() > 0) {
    int hotKey = Serial.read();
    processKey(Serial, hotKey);
  }
}


////////////////////////////////////////////////////////////////////////////////
// preinit() is called from user_init() at SDK startup.
// Runs before C++ global constuctors.
//
#ifndef SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG
#define SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG()
#endif

extern "C" void preinit (void) {
    abendHandlerInstall();
    SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG();
}


////////////////////////////////////////////////////////////////////////////////
// Show merging together multiple custom_crash_callback functions
// A global define of SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO with a unique
// function name, enables this custom_crash_callback wrapper to hold
// multiple callbacks.
#ifdef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO
extern "C" {
  extern void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);

  #ifdef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG
  extern void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);
  #else
  #define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG(...);
  #endif

  extern void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end) {
      SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(rst_info, stack, stack_end);
      SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG(rst_info, stack, stack_end);
  }
}
#endif  // SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO
