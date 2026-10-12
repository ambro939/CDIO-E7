/*
 * ══════════════════════════════════════════════════════════════
 *  RECEIVER DEBUG (BANG-BANG / DIGITAL SWITCH MODE)
 * ══════════════════════════════════════════════════════════════
 *  Modo simplificado sin sensibilidad proporcional:
 *    - Eje Y ARRIBA   → Motor DC Adelante (Potencia Máxima)
 *    - Eje Y ABAJO    → Motor DC Atrás (Potencia Máxima)
 *    - Eje Y CENTRO   → Motor DC Detenido
 *
 *    - Eje X IZQUIERDA → Servo a 0° (90° a la izquierda)
 *    - Eje X DERECHA   → Servo a 180° (90° a la derecha)
 *    - Eje X CENTRO    → Servo a 90° (Centro)
 *
 *  Mantiene Bluetooth Serial ("AutoRC_Debug") para diagnóstico.
 * ══════════════════════════════════════════════════════════════
 */

#include <esp_now.h>
#include <WiFi.h>
#include <ESP32Servo.h>
#include <BluetoothSerial.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

BluetoothSerial SerialBT;

// ── Pines según schematic (U1) ──
#define EN_PIN     13   // IO13 → 3.3V constante para R_EN y L_EN del BTS7960
#define SERVO_PIN  18   // IO18 → señal servo MG996R
#define RPWM_PIN   32   // IO32 → BTS7960 RPWM (adelante)
#define LPWM_PIN   33   // IO33 → BTS7960 LPWM (atrás)

// ── Constantes de Control y Deadzone ──
#define CENTER_VAL     2048
#define DEADZONE_X     600   // Margen amplio para evitar lecturas dudosas
#define DEADZONE_Y     600

// Ángulos fijos del servo
#define SERVO_LEFT     0     // 90° a la izquierda del centro
#define SERVO_CENTER   90    // Centro neutro
#define SERVO_RIGHT    180   // 90° a la derecha del centro

// Potencia fija del motor (0-255)
#define MOTOR_POWER    255   // 100% de potencia para asegurar movimiento

Servo steering;

struct JoystickData {
  int x;
  int y;
};

unsigned long lastReceiveTime = 0;
#define SIGNAL_TIMEOUT 500

void logMsg(String msg) {
  Serial.println(msg);
  if (SerialBT.hasClient()) {
    SerialBT.println(msg);
  }
}

void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(JoystickData)) return;

  JoystickData joy;
  memcpy(&joy, data, sizeof(joy));
  lastReceiveTime = millis();

  // ════════════════════════════════════════════════════
  // 1. CONTROL DE DIRECCIÓN (SERVO) - ON / OFF
  // ════════════════════════════════════════════════════
  int targetAngle = SERVO_CENTER;
  String srvState = "CENTRO (90°)";

  if (joy.x < (CENTER_VAL - DEADZONE_X)) {
    targetAngle = SERVO_LEFT;
    srvState = "IZQUIERDA (0°)";
  } else if (joy.x > (CENTER_VAL + DEADZONE_X)) {
    targetAngle = SERVO_RIGHT;
    srvState = "DERECHA (180°)";
  } else {
    targetAngle = SERVO_CENTER;
    srvState = "CENTRO (90°)";
  }

  steering.write(targetAngle);

  // ════════════════════════════════════════════════════
  // 2. CONTROL DE MOTOR DC - ON / OFF / REVERSA
  // ════════════════════════════════════════════════════
  String mtrState = "STOP";

  if (joy.y > (CENTER_VAL + DEADZONE_Y)) {
    // Adelante
    analogWrite(RPWM_PIN, MOTOR_POWER);
    analogWrite(LPWM_PIN, 0);
    mtrState = "ADELANTE (FULL)";
  } else if (joy.y < (CENTER_VAL - DEADZONE_Y)) {
    // Atrás
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, MOTOR_POWER);
    mtrState = "ATRAS (FULL)";
  } else {
    // Parado
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, 0);
    mtrState = "STOP";
  }

  // Telemetría periódica
  static int count = 0;
  if (++count % 4 == 0) {
    String logText = "X:" + String(joy.x) + " -> " + srvState +
                     " | Y:" + String(joy.y) + " -> " + mtrState;
    logMsg(logText);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500);

  SerialBT.begin("AutoRC_Debug");

  logMsg("\n=============================================");
  logMsg("   ESP32 RECEIVER - MODO SWITCH (DEBUG)      ");
  logMsg("=============================================");

  // Configuración de pines de motor
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);  // Enviar 3.3V constantes para habilitar R_EN y L_EN
  pinMode(RPWM_PIN, OUTPUT);
  pinMode(LPWM_PIN, OUTPUT);
  analogWrite(RPWM_PIN, 0);
  analogWrite(LPWM_PIN, 0);

  // Configuración de servo
  steering.attach(SERVO_PIN, 500, 2400);
  steering.write(SERVO_CENTER);
  logMsg("[INFO] Servo en posición central (90°)");

  // ── Auto-Test inicial de hardware (comprueba que responde) ──
  logMsg("\n--- TEST RAPIDO DE HARDWARE ---");
  logMsg("1. Servo: Izq (0°) -> Der (180°) -> Centro (90°)");
  steering.write(SERVO_LEFT);
  delay(400);
  steering.write(SERVO_RIGHT);
  delay(400);
  steering.write(SERVO_CENTER);
  delay(400);

  logMsg("2. Motor: Pulso adelante 0.5s...");
  analogWrite(RPWM_PIN, 200);
  analogWrite(LPWM_PIN, 0);
  delay(500);

  logMsg("3. Motor: Stop...");
  analogWrite(RPWM_PIN, 0);
  analogWrite(LPWM_PIN, 0);
  delay(200);

  logMsg("4. Motor: Pulso atras 0.5s...");
  analogWrite(RPWM_PIN, 0);
  analogWrite(LPWM_PIN, 200);
  delay(500);

  logMsg("5. Motor: Stop...");
  analogWrite(RPWM_PIN, 0);
  analogWrite(LPWM_PIN, 0);
  logMsg("--- TEST FINALIZADO ---\n");

  // Iniciar WiFi y ESP-NOW
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    logMsg("[ERROR] No se pudo inicializar ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onDataReceive);
  logMsg("[OK] Listo. Mueve el joystick a los extremos para probar.\n");
}

void loop() {
  // Failsafe: detener si se pierde la señal del control
  if (millis() - lastReceiveTime > SIGNAL_TIMEOUT && lastReceiveTime > 0) {
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, 0);
    steering.write(SERVO_CENTER);
  }

  delay(20);
}
