#include <esp_now.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ← Replace with servo ESP32 MAC
uint8_t servoMAC[] = {0x68, 0x09, 0x47, 0x25, 0xD4, 0x38};

esp_now_peer_info_t peerInfo;

struct JoystickData {
  int x;
  int y;
};

void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sent ✓" : "Failed ✗");
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  memcpy(peerInfo.peer_addr, servoMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("Joystick sender ready!");
}

void loop() {
  JoystickData joystick;
  joystick.x = analogRead(33);
  joystick.y = analogRead(35);

  esp_now_send(servoMAC, (uint8_t *)&joystick, sizeof(joystick));

  Serial.print("Sending X: "); Serial.println(joystick.x);
  delay(20);
}