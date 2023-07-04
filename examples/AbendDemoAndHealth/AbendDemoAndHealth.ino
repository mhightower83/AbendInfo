/*
  WIP

   AbendDemoAndHealth.ino - An example to generate some of the various ways a
   sketch could crash and show case the AbendInfo library and the BacktraceLog
   libraries sharing `custom_crash_callback`. Expanded to include Network
   Health Monitoring via ping.

   For how to use, see calls to `abendHandlerInstall()` and `abendInfoReport()`
   in `setup()` and `custom_crash_callback(...)` definition.

Log:
   Using the AbendNetworkHealth component of this library with a problematic
   Sketch has shown that there are times when ping stops working. After 20
   minutes a restart occurs, during that 20 minute period the Sketch did not
   respond to web access. The restarts have occured in up to 4 days. The orignal
   problem of not responding commonly occured around 2 weeks. I suspect in the
   case of using AbendNetworkHealth, if we wait longer than 20 minutes, it would
   have continued to work.

   TODO: Use etharp_request instead of ping and check for empty ARP cache.


*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <AbendInfo.h>
#include <AbendNetworkHealth.h>
#include <BacktraceLog.h>
BacktraceLog backtraceLog;

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK  "your-password"
#endif
const char* ssid = STASSID;
const char* password = STAPSK;

void processKey(Print& out, int hotKey);


void setup(void) {
  // Install BP handler and replace Unhandled Exception handling. And updates
  // Arduino's copy of rst_info with corrected/missed details.
  // As an alternative, this call could be moved into `preinit()`.
  // abendHandlerInstall();

  Serial.begin(115200);
  delay(200);    // This delay helps when using the 'Modified Serial monitor' otherwise it is not needed.
  Serial.printf_P(PSTR("\r\n\r\nDemo Sketch of AbendInfo, AbendNetworkHealth, and BacktraceLog ...\r\n\r\n"));

  // Report on previous crash info and state
  // Print ESP.getResetInfo() with expanded description.
  abendInfoReport(Serial);

  Serial.println();
  backtraceLog.report(Serial);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println(F("\r\nA WiFi connection attempt has been started."));

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.printf_P(PSTR("\r\nWiFi connected\r\n"));
  Serial.printf_P(PSTR("  %-20S %s\r\n"), PSTR("IP address:"), WiFi.localIP().toString().c_str());
  Serial.printf_P(PSTR("  %-20S %s\r\n"), PSTR("Gateway IP address: "), WiFi.gatewayIP().toString().c_str());

  abendEnableNetworkMonitor(WL_CONNECTED == WiFi.status()); // only enable on successful WiFi connect.

  Serial.println();
  processKey(Serial, '?');
}

void loop(void) {
  if (! abendIsHeapOK()) {
      panic();
  }
  if (! abendIsNetworkOK()) {
      if (!WiFi.localIP().isSet()) {
          Serial.printf_P(PSTR("\nLost IP Address\n"));
      }
      panic();
  }

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
    abendHandlerInstall(true);  // Moved to ProcessKey.cpp for testing
    SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG();
}


////////////////////////////////////////////////////////////////////////////////
// Show merging together multiple custom_crash_callback functions
// A global define of SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO with a unique
// function name, enables this custom_crash_callback wrapper to hold
// multiple callbacks.
extern "C" void custom_crash_callback(struct rst_info *rst_info, uint32_t stack, uint32_t stack_end) {
    struct rst_info info = *rst_info;
    SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDINFO(&info, stack, stack_end);
    SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_ABENDNETORKHEALTH(&info, stack, stack_end);
    SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG(rst_info, stack, stack_end);
}
