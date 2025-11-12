
#include "ESP32_NOW.h"
#include "WiFi.h"


#include <esp_mac.h>  // For the MAC2STR and MACSTR macros
#include <string>

/* Definitions */

#define ESPNOW_WIFI_CHANNEL 0
#define OFF (0)
#define ON (1)
#define VOL_UP (2)
#define VOL_DOWN (3)
#define CHAN_UP (4)
#define CHAN_DOWN (5)

/* Variables */
static int off_on = 0;
const uint8_t broadcastAddress[] = {0x40, 0x4C, 0xCA, 0x5B, 0xDC, 0x38};
esp_now_peer_info_t peerInfo;
static uint64_t old_millis = 0;
uint8_t data = OFF;
uint8_t *send_data = &data;
String broadcast_message;

/* Classes */

/* Global Variables */

uint32_t msg_count = 0;

/* Main */

void setup() {
  Serial.begin(115200);

  // Initialize the Wi-Fi module
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  Serial.println("ESP-NOW Example - Broadcast Master");
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  Serial.printf("ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());

  //Serial.println("Setup complete. Broadcasting messages every 5 seconds.");

  pinMode(2, INPUT_PULLUP); // Set pin 2 to be an input w/ pull up resistor
}

void loop() {
if ((millis() - old_millis) > 200) // Button debounce
{ 
  old_millis = millis();
  if (!digitalRead(2)) // If button pressed
  {
    if (off_on == 0)
    {
      data = ON;
      off_on = 1;
      broadcast_message = "ON";
    }
    else
    {
      data = OFF;
      off_on = 0;
      broadcast_message = "OFF";
    }
    Serial.printf("Broadcasting message: %s(%d)\n", broadcast_message, data);
    if (esp_now_send(peerInfo.peer_addr, send_data, 1) != ESP_OK)
    {
      Serial.printf("Failed to broadcast message\n");
    }
    else
    {
      Serial.printf("Message broadcasted successfully\n");
    }
    while (!digitalRead(2)); // Wait while button held
  }
}
}
