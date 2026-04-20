#include <esp_now.h>
#include <WiFi.h>

#define LED_PIN 2          // Optional: status LED
#define TX_PIN 4           // Placeholder IR transmit pin
#define IR_PROTOCOL NEC    // Placeholder protocol
#define IR_BITS 32         // Placeholder bit length

// Placeholder IR send macro
#define ir_send(pin, protocol, code, bits) \
  Serial.printf("IR SEND → Pin: %d, Protocol: %s, Code: 0x%X, Bits: %d\n", pin, #protocol, code, bits)

// Define your incoming command structure
typedef struct struct_message {
  char command[32];  // Raw command string
} struct_message;

struct_message incomingCommand;

// Callback when ESP-NOW data is received
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  struct_message incomingCommand;
  memcpy(&incomingCommand, incomingData, sizeof(incomingCommand));

  Serial.println("ESP-NOW command received:");
  Serial.println(incomingCommand.command);

  String translatedCode = sendToTranslator(incomingCommand.command);

  if (translatedCode.length() > 0) {
    uint32_t irCode = strtoul(translatedCode.c_str(), NULL, 16);
    ir_send(TX_PIN, IR_PROTOCOL, irCode, IR_BITS);
  } else {
    Serial.println("No translated code received.");
  }
}


// Placeholder translator function
String sendToTranslator(String rawCommand) {
  // TODO: Replace with actual logic (e.g., HTTP request, serial relay, etc.)
  Serial.println("Sending to translator...");
  delay(100);  // Simulate processing delay

  // Simulated translation result
  if (rawCommand == "power") return "0x20DF10EF";  // Example NEC code
  if (rawCommand == "volume_up") return "0x20DF40BF";
  if (rawCommand == "volume_down") return "0x20DFC03F";

  return "";  // Unknown command
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  WiFi.mode(WIFI_STA);

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
