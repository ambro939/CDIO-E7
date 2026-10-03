#include <esp_now.h>
#include <WiFi.h>

// Called automatically when a message arrives
void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  char message[len + 1];
  memcpy(message, data, len);
  message[len] = '\0';
  Serial.print("Message received: ");
  Serial.println(message);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onDataReceive);
  Serial.println("Receiver ready, waiting for messages...");
}

void loop() {}