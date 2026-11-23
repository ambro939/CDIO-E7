/* MAC: 68:09:47:25:D4:38.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <esp_arduino_version.h>
#include <math.h>
#include <string.h>

constexpr uint8_t RADIO_CHANNEL = 1;
constexpr uint16_t MAGIC = 0x5243;
constexpr uint8_t VERSION = 1;
enum CarState : uint8_t { WAIT_NEUTRAL = 0, ACTIVE = 1, SIGNAL_LOST = 2 };
struct __attribute__((packed)) Command {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint32_t sequence;
  uint16_t x;
  uint16_t y;
};
struct __attribute__((packed)) Telemetry {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint32_t sequence;
  int16_t pwm;
  uint8_t angle;
  uint8_t state;
};
static_assert(sizeof(Command) == 12, "Command layout");
static_assert(sizeof(Telemetry) == 12, "Telemetry layout");
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool ready = false;
bool txBusy = false;
uint32_t sendErrors = 0;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onDataSent(const esp_now_send_info_t *, esp_now_send_status_t status) {
#else
void onDataSent(const uint8_t *, esp_now_send_status_t status) {
#endif
  portENTER_CRITICAL(&mux);
  txBusy = false;
  if (status != ESP_NOW_SEND_SUCCESS) ++sendErrors;
  portEXIT_CRITICAL(&mux);
}

bool sendPacket(const uint8_t *mac, const void *packet, size_t length) {
  portENTER_CRITICAL(&mux);
  bool available = !txBusy;
  if (available) txBusy = true;
  portEXIT_CRITICAL(&mux);
  if (!available) return false; // No apilar envios pendientes.
  if (esp_now_send(mac, (const uint8_t *)packet, length) != ESP_OK) {
    portENTER_CRITICAL(&mux);
    txBusy = false;
    ++sendErrors;
    portEXIT_CRITICAL(&mux);
    return false;
  }
  return true;
}

bool beginRadio(const uint8_t *peerMAC, esp_now_recv_cb_t callback) {
  if (!WiFi.mode(WIFI_STA)) return false;
  WiFi.setSleep(false);
  if (esp_wifi_set_channel(RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK)
    return false;
  Serial.print("MAC STA local: "); Serial.println(WiFi.macAddress());
  if (esp_now_init() != ESP_OK) return false;
  if (esp_now_register_recv_cb(callback) != ESP_OK) return false;
  if (esp_now_register_send_cb(onDataSent) != ESP_OK) return false;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerMAC, 6);
  peer.channel = RADIO_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false; // Se conserva la configuracion original.
  return esp_now_add_peer(&peer) == ESP_OK;
}

constexpr uint8_t EN_PIN = 13, SERVO_PIN = 18, RPWM_PIN = 32, LPWM_PIN = 33;
const uint8_t senderMAC[] = {0x68, 0x09, 0x47, 0x26, 0xD4, 0x48};
constexpr int CENTER = 1800, DEADZONE = 150;
constexpr int SERVO_MIN = 0, SERVO_MAX = 180, SERVO_CENTER = 90;
constexpr uint32_t SIGNAL_TIMEOUT_MS = 500, CONTROL_MS = 5, TELEMETRY_MS = 100;
constexpr float FULL_SCALE_RAMP_MS = 100.0f;
// ESP32 clasico: canales 0/1 para motor, canal 2 para servo.
// El servo usa LEDC directamente para evitar colisiones con ESP32Servo.
Command latest = {};
uint32_t receivedAt = 0;
bool received = false, mustRearm = false;
bool motorPinsReady = false, servoReady = false, armed = false;
float applied = 0.0f;
int appliedPWM = 0, lastServoAngle = SERVO_CENTER;
uint32_t lastControl = 0, lastTelemetry = 0;
CarState state = WAIT_NEUTRAL;

void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !data || len != sizeof(Command) ||
      memcmp(info->src_addr, senderMAC, 6) != 0) return;
  Command p;
  memcpy(&p, data, sizeof(p));
  if (p.magic != MAGIC || p.version != VERSION || p.type != 1 ||
      p.x > 4095 || p.y > 4095) return;
  uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  // Conservar perdida de enlace aunque un paquete llegue antes del loop.
  if (received && uint32_t(now - receivedAt) > SIGNAL_TIMEOUT_MS) mustRearm = true;
  latest = p;
  receivedAt = now;
  received = true;
  portEXIT_CRITICAL(&mux);
}

int targetFromJoystick(int y) {
  if (y > CENTER + DEADZONE)
    return constrain(map(y, CENTER + DEADZONE, 4095, 0, 255), 0L, 255L);
  if (y < CENTER - DEADZONE)
    return -constrain(map(y, CENTER - DEADZONE, 0, 0, 255), 0L, 255L);
  return 0;
}

void writeMotor(int pwm) {
  // Siempre apagar primero la rama opuesta.
  if (pwm > 0) { ledcWrite(LPWM_PIN, 0); ledcWrite(RPWM_PIN, pwm); }
  else { ledcWrite(RPWM_PIN, 0); ledcWrite(LPWM_PIN, -pwm); }
  appliedPWM = pwm;
}

void writeServo(int angle) {
  angle = constrain(angle, SERVO_MIN, SERVO_MAX);
  // Misma envolvente del original: 700..2300 us a 50 Hz.
  uint32_t pulse = map(angle, 0, 180, 700, 2300);
  uint32_t duty = (pulse * 65536UL + 10000UL) / 20000UL;
  ledcWrite(SERVO_PIN, duty);
  lastServoAngle = angle;
}

void stopMotor() {
  applied = 0;
  if (motorPinsReady) writeMotor(0);
  else { digitalWrite(RPWM_PIN, LOW); digitalWrite(LPWM_PIN, LOW); }
  digitalWrite(EN_PIN, LOW);
  armed = false;
}

void setup() {
  Serial.begin(115200);
  pinMode(EN_PIN, OUTPUT); digitalWrite(EN_PIN, LOW);
  pinMode(RPWM_PIN, OUTPUT); digitalWrite(RPWM_PIN, LOW);
  pinMode(LPWM_PIN, OUTPUT); digitalWrite(LPWM_PIN, LOW);
  // No se desactiva la proteccion de brownout.
  bool rightOK = ledcAttachChannel(RPWM_PIN, 1000, 8, 0);
  bool leftOK = ledcAttachChannel(LPWM_PIN, 1000, 8, 1);
  if (rightOK) ledcWrite(RPWM_PIN, 0);
  if (leftOK) ledcWrite(LPWM_PIN, 0);
  motorPinsReady = rightOK && leftOK;
  servoReady = ledcAttachChannel(SERVO_PIN, 50, 16, 2);
  if (servoReady) writeServo(SERVO_CENTER);
  if (!motorPinsReady || !servoReady ||
      ledcReadFreq(RPWM_PIN) != 1000 || ledcReadFreq(LPWM_PIN) != 1000 ||
      ledcReadFreq(SERVO_PIN) != 50) {
    Serial.println("ERROR PWM: motor deshabilitado; reinicie tras corregir.");
    return;
  }
  if (!beginRadio(senderMAC, onDataReceive)) {
    Serial.println("ERROR ESP-NOW: motor deshabilitado."); return;
  }
  lastControl = millis();
  ready = true;
  Serial.println("AUTO listo. Acelerador en neutro para habilitar.");
}

void loop() {
  if (!ready) { delay(1); return; }
  Command cmd;
  uint32_t stamp;
  bool seen, rearm;
  portENTER_CRITICAL(&mux);
  cmd = latest; stamp = receivedAt; seen = received;
  rearm = mustRearm; mustRearm = false;
  portEXIT_CRITICAL(&mux);
  uint32_t now = millis();
  bool fresh = seen && uint32_t(now - stamp) <= SIGNAL_TIMEOUT_MS;
  if (rearm) stopMotor();
  if (!fresh) {
    stopMotor();
    state = seen ? SIGNAL_LOST : WAIT_NEUTRAL;
    if (lastServoAngle != SERVO_CENTER) writeServo(SERVO_CENTER);
    lastControl = now;
  } else {
    bool neutral = cmd.y >= CENTER - DEADZONE && cmd.y <= CENTER + DEADZONE;
    if (!armed && neutral) {
      applied = 0; writeMotor(0);
      digitalWrite(EN_PIN, HIGH);
      armed = true; lastControl = now;
    }
    state = armed ? ACTIVE : WAIT_NEUTRAL;
    if (uint32_t(now - lastControl) >= CONTROL_MS) {
      uint32_t dt = now - lastControl; lastControl = now;
      // Si el loop se atrasa, no recuperar todo el salto en una sola escritura.
      if (dt > 20) dt = 20;
      int angle = constrain(map(cmd.x, 0, 4095, SERVO_MIN, SERVO_MAX),
                            long(SERVO_MIN), long(SERVO_MAX));
      if (abs(angle - lastServoAngle) >= 2) writeServo(angle);
      int target = armed ? targetFromJoystick(cmd.y) : 0;
      // Al invertir, publicar al menos un ciclo en cero antes de invertir PWM.
      int stage = ((applied > 0 && target < 0) || (applied < 0 && target > 0)) ? 0 : target;
      float step = 255.0f * dt / FULL_SCALE_RAMP_MS;
      if (applied < stage) applied = fminf(applied + step, float(stage));
      else if (applied > stage) applied = fmaxf(applied - step, float(stage));
      writeMotor(int(lroundf(applied)));
    }
  }
  if (uint32_t(now - lastTelemetry) >= TELEMETRY_MS) {
    lastTelemetry = now;
    Telemetry p = {MAGIC, VERSION, 2, cmd.sequence, int16_t(appliedPWM),
                   uint8_t(lastServoAngle), uint8_t(state)};
    sendPacket(senderMAC, &p, sizeof(p));
  }
  delay(1); // Ceder CPU; no es una espera de control de 20/150 ms.
}
