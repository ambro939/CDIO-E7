#include <esp_now.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ← Put the receiver's MAC address here
uint8_t receiverMAC[] = {0x68, 0x09, 0x47, 0x25, 0xD4, 0x38};

esp_now_peer_info_t peerInfo;

void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success ✓" : "Failed ✗");
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("Sender ready!");


  
}

void loop() {
    // code for joystick test
  int y = analogRead(34);
  int x = analogRead(35);


  char message[50];
  sprintf(message, "Joystick: (%d, %d)", x, y);
  esp_now_send(receiverMAC, (uint8_t *)message, strlen(message));
  Serial.println("Message sent!");
  delay(100);
}