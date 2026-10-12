/*
 * ══════════════════════════════════════════════════════
 *  RECEIVER — Auto RC (ESP32-WROOM-32U)
 * ══════════════════════════════════════════════════════
 *  MAC de este ESP32: 68:09:47:25:D4:38
 *  Recibe datos del joystick vía ESP-NOW y controla:
 *    - Servo MG996R (dirección)
 *    - Motor DC vía BTS7960 IBT-2 (velocidad)
 *
 *  Conexiones según schematic (U1):
 *    SERVO_SIG → GPIO 18 (IO18, pin 30)
 *    RPWM      → GPIO 32 (IO32, pin 8)  → motor adelante
 *    LPWM      → GPIO 33 (IO33, pin 9)  → motor atrás
 *    3V3       → IO12 (pin 16) → BTS7960 R_EN y L_EN
 *    +5V       → pin 39 → LM2596 OUT+ → Servo VCC, BTS7960 VCC
 *    GND       → pin 1  → común
 * ══════════════════════════════════════════════════════
 */

#include <esp_now.h>
#include <WiFi.h>
#include <ESP32Servo.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ── Pines según schematic (U1) ──
#define EN_PIN     13   // IO13 → 3.3V constante para R_EN y L_EN del BTS7960
#define SERVO_PIN  18   // IO18 → señal servo MG996R
#define RPWM_PIN   32   // IO32 → BTS7960 RPWM (motor adelante)
#define LPWM_PIN   33   // IO33 → BTS7960 LPWM (motor atrás)

// ── Objeto servo ──
Servo steering;

// ── Estructura de datos recibida ──
// IMPORTANTE: debe ser IDÉNTICA a la del sender
struct JoystickData {
  int x;      // valor del eje X (0-4095) → controla el servo
  int y;      // valor del eje Y (0-4095) → controla el motor DC
};

// ── Seguridad: timeout si se pierde la señal ──
unsigned long lastReceiveTime = 0;
#define SIGNAL_TIMEOUT  500   // ms sin señal → detener todo

// ── Callback: se ejecuta automáticamente al recibir datos ──
void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  // Verificar que el paquete tiene el tamaño correcto
  if (len != sizeof(JoystickData)) return;

  JoystickData joy;
  memcpy(&joy, data, sizeof(joy));

  // Actualizar timestamp de última recepción
  lastReceiveTime = millis();

  // ═══════════════════════════════════════════
  //  SERVO — Dirección (eje X del joystick)
  // ═══════════════════════════════════════════
  // Mapear joystick X (0-4095) a ángulo servo (0-180)
  int angle = map(joy.x, 0, 4095, 0, 180);
  angle = constrain(angle, 0, 180);
  steering.write(angle);

  // ═══════════════════════════════════════════
  //  MOTOR DC — Velocidad (eje Y del joystick)
  // ═══════════════════════════════════════════
  int center = 2700;      // centro teórico del joystick
  int deadzone = 200;     // zona muerta: joystick centrado = motor parado

  if (joy.y > center + deadzone) {
    // ── Adelante ──
    int speed = map(joy.y, center + deadzone, 4095, 0, 255);
    speed = constrain(speed, 0, 255);
    analogWrite(RPWM_PIN, speed);
    analogWrite(LPWM_PIN, 0);

  } else if (joy.y < center - deadzone) {
    // ── Atrás ──
    int speed = map(joy.y, center - deadzone, 0, 0, 255);
    speed = constrain(speed, 0, 255);
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, speed);

  } else {
    // ── Zona muerta: motor detenido ──
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, 0);
  }

  // Debug (comentar en uso real para mayor velocidad)
  Serial.print("X: ");
  Serial.print(joy.x);
  Serial.print(" → Servo: ");
  Serial.print(angle);
  Serial.print("°\tY: ");
  Serial.print(joy.y);
  Serial.print(" → Motor: ");
  if (joy.y > center + deadzone) {
    Serial.print("ADELANTE ");
    Serial.println(map(joy.y, center + deadzone, 4095, 0, 100));
  } else if (joy.y < center - deadzone) {
    Serial.print("ATRAS ");
    Serial.println(map(joy.y, center - deadzone, 0, 0, 100));
  } else {
    Serial.println("STOP");
  }
}

void setup() {
  // Desactivar detector de brownout
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n============================");
  Serial.println(" RECEIVER - Auto RC ESP-NOW");
  Serial.println("============================");

  // ── Configurar pines del motor ──
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);  // Enviar 3.3V constantes a R_EN y L_EN para habilitar el driver
  pinMode(RPWM_PIN, OUTPUT);
  pinMode(LPWM_PIN, OUTPUT);
  analogWrite(RPWM_PIN, 0);    // motor apagado al inicio
  analogWrite(LPWM_PIN, 0);

  // ── Configurar servo ──
  steering.attach(SERVO_PIN, 500, 2400);
  steering.write(90);          // servo centrado al inicio
  Serial.println("Servo centrado en 90°");

  // ── Inicializar WiFi y ESP-NOW ──
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW no se pudo inicializar");
    return;
  }

  // Registrar callback de recepción
  esp_now_register_recv_cb(onDataReceive);

  Serial.println("Receptor listo! Esperando datos del joystick...\n");
}

void loop() {
  // ═══════════════════════════════════════════
  //  SEGURIDAD: Si no se recibe señal, parar
  // ═══════════════════════════════════════════
  // Si pasan más de 500ms sin recibir datos del sender,
  // detener el motor y centrar el servo por seguridad.
  // Esto previene que el auto siga andando si el sender
  // se desconecta, se queda sin batería, o pierde señal.

  if (millis() - lastReceiveTime > SIGNAL_TIMEOUT) {
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, 0);
    steering.write(90);
  }

  delay(20);
}
