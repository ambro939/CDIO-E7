#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

void setup() {
  // Disable brownout detector to prevent reset loops from USB power drops
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000); // Give serial monitor time to connect

  Serial.println("\n--- ESP32 Test & MAC Reader ---");

  // Put WiFi in Station mode (required to read the MAC)
  WiFi.mode(WIFI_STA);
  delay(200);

  // Read and print MAC Address
  String mac = WiFi.macAddress();
  Serial.print("ESP32 MAC Address: ");
  Serial.println(mac);

  // Print formatted ready for code (C array format)
  Serial.print("Format for code:   {");
  for (int i = 0; i < 6; i++) {
    Serial.print("0x");
    Serial.print(mac.substring(i * 3, i * 3 + 2));
    if (i < 5) Serial.print(", ");
  }
  Serial.println("}");
  Serial.println("--------------------------------\n");
}

void loop() {
  // Heartbeat to prove code is running continuously
  Serial.println("ESP32 is alive and running... (heartbeat)");
  delay(3000);
}