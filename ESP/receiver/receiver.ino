#include <esp_now.h>
#include <WiFi.h>
#include <ESP32Servo.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ── Pines ──
#define EN_PIN     13
#define SERVO_PIN  18
#define RPWM_PIN   32
#define LPWM_PIN   33

// ── Motor PWM config ──
#define MOTOR_FREQ  1000  // 1 kHz para BTS7960
#define MOTOR_RES   8     // 8 bits (0-255)

Servo steering;

struct JoystickData {
  int x;
  int y;
};

volatile int rawX = 2048;
volatile int rawY = 2048;
volatile unsigned long lastReceiveTime = 0;
#define SIGNAL_TIMEOUT 500

int lastServoAngle = 90;

void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(JoystickData)) return;
  JoystickData joy;
  memcpy(&joy, data, sizeof(joy));
  rawX = joy.x;
  rawY = joy.y;
  lastReceiveTime = millis();
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- RECEIVER CORREGIDO ---");

  // ── Servo: dejar que ESP32Servo gestione su timer ──
  steering.setPeriodHertz(50);
  steering.attach(SERVO_PIN, 700, 2300);
  steering.write(90);

  // ── Motor: usar ledcAttach (timer separado) ──
  ledcAttach(RPWM_PIN, MOTOR_FREQ, MOTOR_RES);
  ledcAttach(LPWM_PIN, MOTOR_FREQ, MOTOR_RES);
  ledcWrite(RPWM_PIN, 0);
  ledcWrite(LPWM_PIN, 0);

  // ── Enable del BTS7960 ──
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);

  // ── ESP-NOW ──
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR al iniciar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onDataReceive);
  Serial.println("Listo y esperando datos...");
}

void loop() {
  // Seguridad: pérdida de señal
  if (millis() - lastReceiveTime > SIGNAL_TIMEOUT && lastReceiveTime > 0) {
    ledcWrite(RPWM_PIN, 0);
    ledcWrite(LPWM_PIN, 0);
    if (lastServoAngle != 90) {
      steering.write(90);
      lastServoAngle = 90;
    }
    delay(20);
    return;
  }

  // ── SERVO ──
  int targetAngle = map(rawX, 0, 4095, 0, 180);
  targetAngle = constrain(targetAngle, 0, 180);

  if (abs(targetAngle - lastServoAngle) >= 2) {
    steering.write(targetAngle);
    lastServoAngle = targetAngle;
  }

  // ── MOTOR DC ──
  int center = 1800;
  int deadzone = 150;

  if (rawY > center + deadzone) {
    int speed = map(rawY, center + deadzone, 4095, 0, 255);
    speed = constrain(speed, 0, 255);
    ledcWrite(LPWM_PIN, 0);
    ledcWrite(RPWM_PIN, speed);
  } else if (rawY < center - deadzone) {
    int speed = map(rawY, center - deadzone, 0, 0, 255);
    speed = constrain(speed, 0, 255);
    ledcWrite(RPWM_PIN, 0);
    ledcWrite(LPWM_PIN, speed);
  } else {
    ledcWrite(RPWM_PIN, 0);
    ledcWrite(LPWM_PIN, 0);
  }

  delay(20);
}