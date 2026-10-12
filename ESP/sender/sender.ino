/*
 * ══════════════════════════════════════════════════
 *  SENDER — Joystick Controller (ESP32-WROOM-32U)
 * ══════════════════════════════════════════════════
 *  MAC de este ESP32: 68:09:47:26:D4:48
 *  Envía datos del joystick al receptor vía ESP-NOW
 *
 *  Conexiones:
 *    VRy  → GPIO 32
 *    VRx  → GPIO 33
 *    5v   → 3.3V
 *    GND  → GND
 * ══════════════════════════════════════════════════
 */

#include <esp_now.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ── Pines del Joystick ──
#define JOY_X_PIN  33   // VRx → IO33 (dirección: izquierda/derecha → servo)
#define JOY_Y_PIN  32   // VRy → IO32 (aceleración: adelante/atrás → motor)

// ── MAC del receptor (ESP32 con motor y servo) ──
uint8_t receiverMAC[] = {0x68, 0x09, 0x47, 0x25, 0xD4, 0x38};

// ── Estructura de datos que se envía ──
struct JoystickData {
  int x;      // valor del eje X (0-4095) → controla el servo
  int y;      // valor del eje Y (0-4095) → controla el motor DC
};

esp_now_peer_info_t peerInfo;

// Callback que se ejecuta cada vez que se envía un paquete
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Descomentar la siguiente línea solo para depuración:
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void setup() {
  // Desactivar detector de brownout (previene reinicios por caídas de voltaje USB)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=============================");
  Serial.println(" SENDER - Joystick Controller");
  Serial.println("=============================");

  // Inicializar WiFi en modo estación (requerido para ESP-NOW)
  WiFi.mode(WIFI_STA);

  // Inicializar ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW no se pudo inicializar");
    return;
  }

  // Registrar callback de envío
  esp_now_register_send_cb(onDataSent);

  // Registrar el receptor como peer
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;      // canal automático
  peerInfo.encrypt = false;   // sin encriptación

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ERROR: No se pudo agregar el peer receptor");
    return;
  }

  Serial.println("Sender listo! Enviando datos del joystick...\n");
}

void loop() {
  // Leer valores analógicos del joystick
  JoystickData data;
  data.x = analogRead(JOY_X_PIN);   // 0-4095, controla dirección (servo)
  data.y = analogRead(JOY_Y_PIN);   // 0-4095, controla velocidad (motor)

  // Enviar el struct completo al receptor
  esp_now_send(receiverMAC, (uint8_t *)&data, sizeof(data));

  // Imprimir para depuración (comentar en uso real)
  """
  Serial.print("X: ");
  Serial.print(data.x);
  Serial.print("\tY: ");
  Serial.println(data.y);
  """

  // 50ms entre envíos (20 actualizaciones por segundo)
  delay(150);
}
