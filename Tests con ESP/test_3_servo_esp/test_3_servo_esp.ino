#include <esp_now.h>
#include <WiFi.h>
#include <ESP32Servo.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

Servo myServo;

struct JoystickData {
  int x;
  int y;
};

void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  JoystickData joystick;
  memcpy(&joystick, data, sizeof(joystick));

  int angle = map(joystick.x, 0, 4095, 0, 180);
  myServo.write(angle);

  Serial.print("X: "); Serial.print(joystick.x);
  Serial.print(" → Angle: "); Serial.println(angle);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);

  myServo.attach(18, 500, 2400);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataReceive);
  Serial.println("Servo receiver ready!");
}

void loop() {}