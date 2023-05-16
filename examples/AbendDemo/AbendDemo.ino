/*
   AbendDemo.ino - An example to generate some of the various ways a sketch
   could crash and show cases the AbendInfo library.

   For how to use, see calls to `abendHandlerInstall()` and `abendInfoReport()`
   in `setup()` below.
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <AbendInfo.h>

void processKey(Print& out, int hotKey);


void setup(void) {
  // Patch some common unhandled exception which lead to WDT resets.
  abendHandlerInstall();

  Serial.begin(115200);
  delay(200);    // This delay helps when using the 'Modified Serial monitor' otherwise it is not needed.
  Serial.printf_P(PSTR("\r\n\r\nAbendInfo Demo Sketch ...\r\n\r\n"));

  // Report on previous crash info and state
  abendInfoReport(Serial);

  Serial.println();
  processKey(Serial, '?');
}


void loop(void) {
  if (Serial.available() > 0) {
    int hotKey = Serial.read();
    processKey(Serial, hotKey);
  }
}
