/* MANDO — MAC esperada: 68:09:47:26:D4:48.
 * GPIO33 direccion, GPIO32 acelerador. Joysticks alimentados a 3.3 V.
 * Monitor Serie USB: 115200 baudios. No requiere pantalla.
 */
// Arduino-ESP32 3.x, placa ESP32 Dev Module (ESP32-WROOM-32U).
// Cargar ambos sketches: el protocolo nuevo no es compatible con el original.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <esp_arduino_version.h>
#include <math.h>
#include <string.h>
#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "Instale esp32 by Espressif Systems 3.x"
#endif

constexpr uint8_t RADIO_CHANNEL = 1; // Igual en las dos placas; no conectar a un AP.
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

constexpr uint8_t JOY_X_PIN = 33, JOY_Y_PIN = 32;
const uint8_t receiverMAC[] = {0x68, 0x09, 0x47, 0x25, 0xD4, 0x38};
constexpr uint32_t SEND_MS = 20, PRINT_MS = 100, TELEMETRY_TIMEOUT_MS = 500;
Telemetry latest = {};
uint32_t receivedAt = 0;
bool received = false;
uint32_t sequence = 0, lastSend = 0, lastPrint = 0;

void onDataReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !data || len != sizeof(Telemetry) ||
      memcmp(info->src_addr, receiverMAC, 6) != 0) return;
  Telemetry p;
  memcpy(&p, data, sizeof(p));
  if (p.magic != MAGIC || p.version != VERSION || p.type != 2 ||
      p.pwm < -255 || p.pwm > 255 || p.angle > 180 || p.state > SIGNAL_LOST) return;
  uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  latest = p; receivedAt = now; received = true;
  portEXIT_CRITICAL(&mux);
}

void setup() {
  Serial.begin(115200);
  pinMode(JOY_X_PIN, INPUT); pinMode(JOY_Y_PIN, INPUT);
  analogReadResolution(12);
  // Se mantiene la atenuacion predeterminada del core, como en el original.
  if (!beginRadio(receiverMAC, onDataReceive)) {
    Serial.println("ERROR ESP-NOW: reinicie tras corregir."); return;
  }
  ready = true;
  Serial.println("MANDO listo. PWM y servo son ordenes, no mediciones fisicas.");
}

void loop() {
  if (!ready) { delay(1); return; }
  uint32_t now = millis();
  if (uint32_t(now - lastSend) >= SEND_MS) {
    lastSend = now;
    Command p = {MAGIC, VERSION, 1, ++sequence,
                 uint16_t(analogRead(JOY_X_PIN)), uint16_t(analogRead(JOY_Y_PIN))};
    sendPacket(receiverMAC, &p, sizeof(p));
  }
  if (uint32_t(now - lastPrint) >= PRINT_MS) {
    lastPrint = now;
    Telemetry p;
    uint32_t stamp;
    bool seen;
    portENTER_CRITICAL(&mux);
    p = latest; stamp = receivedAt; seen = received;
    portEXIT_CRITICAL(&mux);
    now = millis();
    char line[96];
    if (!seen || uint32_t(now - stamp) > TELEMETRY_TIMEOUT_MS) {
      snprintf(line, sizeof(line), "AUTO | SIN TELEMETRIA\n");
    } else {
      const char *label = p.state == ACTIVE ? "ACTIVO" :
                          p.state == SIGNAL_LOST ? "SIN ORDENES" : "NEUTRO REQUERIDO";
      int percent = int(lroundf(100.0f * p.pwm / 255.0f));
      snprintf(line, sizeof(line), "AUTO | PWM:%+d%% | Servo:%u | %s\n",
               percent, unsigned(p.angle), label);
    }
    size_t n = strlen(line);
    // Si el buffer esta lleno, omitir esta linea; nunca esperar al Monitor Serie.
    if (Serial.availableForWrite() >= int(n))
      Serial.write((const uint8_t *)line, n);
  }
  delay(1);
}
