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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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
  uint16_t batteryMv; // Voltaje de bateria medido en el auto, en mV.
};
static_assert(sizeof(Command) == 12, "Command layout");
static_assert(sizeof(Telemetry) == 14, "Telemetry layout");
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
constexpr uint8_t OLED_SDA_PIN = 21, OLED_SCL_PIN = 22;
constexpr uint8_t OLED_ADDRESS = 0x3C; // Direccion I2C habitual del SSD1306.
const uint8_t receiverMAC[] = {0x68, 0x09, 0x47, 0x25, 0xD4, 0x38};
constexpr uint32_t SEND_MS = 20, PRINT_MS = 100, TELEMETRY_TIMEOUT_MS = 500;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledReady = false;
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

void drawOLED(bool telemetryFresh, const Telemetry &p) {
  if (!oledReady) return;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);

  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("BATERIA DEL AUTO");
  oled.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  if (telemetryFresh) {
    oled.setTextSize(3);
    oled.setCursor(0, 23);
    oled.print(float(p.batteryMv) / 1000.0f, 2);
    oled.print("V");

    oled.setTextSize(1);
    oled.setCursor(0, 56);
    const char *label = p.state == ACTIVE ? "ACTIVO" :
                        p.state == SIGNAL_LOST ? "SIN ORDENES" : "NEUTRO REQUERIDO";
    oled.print(label);
  } else {
    oled.setTextSize(2);
    oled.setCursor(0, 27);
    oled.print("SIN DATOS");
    oled.setTextSize(1);
    oled.setCursor(0, 56);
    oled.print("Revisar enlace ESP-NOW");
  }

  oled.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(JOY_X_PIN, INPUT); pinMode(JOY_Y_PIN, INPUT);
  analogReadResolution(12);

  // OLED SSD1306 I2C: SDA=21, SCL=22. La pantalla no bloquea el mando si falla.
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  if (oledReady) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("MANDO RC");
    oled.println("Iniciando pantalla...");
    oled.display();
  } else {
    Serial.println("AVISO: no se detecto OLED SSD1306 en 0x3C.");
  }

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
    bool telemetryFresh = seen && uint32_t(now - stamp) <= TELEMETRY_TIMEOUT_MS;
    drawOLED(telemetryFresh, p);

    char line[128];
    if (!telemetryFresh) {
      //snprintf(line, sizeof(line), "AUTO | SIN TELEMETRIA\n");
    } else {
      const char *label = p.state == ACTIVE ? "ACTIVO" :
                          p.state == SIGNAL_LOST ? "SIN ORDENES" : "NEUTRO REQUERIDO";
      int percent = int(lroundf(100.0f * p.pwm / 255.0f));
      snprintf(line, sizeof(line),
               "AUTO | BAT:%u.%02uV | PWM:%+d%% | Servo:%u | %s\n",
               unsigned(p.batteryMv / 1000),
               unsigned((p.batteryMv % 1000) / 10),
               percent, unsigned(p.angle), label);
    }
    size_t n = strlen(line);
    // Si el buffer esta lleno, omitir esta linea; nunca esperar al Monitor Serie.
    if (Serial.availableForWrite() >= int(n))
      Serial.write((const uint8_t *)line, n);
  }


  // DEBUG
  static unsigned long lastDebug = 0;

  if (millis() - lastDebug >= 200) {
    lastDebug = millis();

    int rawX = analogRead(JOY_X_PIN);
    int rawY = analogRead(JOY_Y_PIN);

    Serial.printf(
      "Joystick X: %4d | Joystick Y: %4d\n",
      rawX, rawY
    );
  }



  delay(1);
}
