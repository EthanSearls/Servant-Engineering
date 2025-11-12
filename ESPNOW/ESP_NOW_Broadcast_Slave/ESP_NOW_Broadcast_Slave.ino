#include <esp_now.h>
#include <WiFi.h>
#include "translator.h"

#define LED_PIN 2
#define TX_PIN 4
#define IR_PROTOCOL NEC
#define IR_BITS 32

#define ir_send(pin, protocol, code, bits) \

uint8_t incomingCommand;

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len >= 1) {
    incomingCommand = incomingData[0];
    const char* label = translateCommand(incomingCommand);

    Serial.print("ESP-NOW command received: ");
    Serial.println(label);

    if (strcmp(label, "unknown") != 0) {
      ir_send(TX_PIN, IR_PROTOCOL, incomingCommand, IR_BITS);
    } else {
      Serial.println("Unrecognized command code.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  WiFi.mode(WIFI_STA);

  Serial.print("ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW receiver ready.");
}

void loop() {
  // Nothing needed here
}
