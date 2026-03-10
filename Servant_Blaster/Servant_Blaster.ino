#include <Arduino.h>
#include <Preferences.h>
#include <IRremote.hpp>
#include "WebRemote.h"

// --- PINS ---
#define IR_RECEIVE_PIN 15 // Change to your IR Receiver Pin
#define IR_SEND_PIN 4     // Change to your IR LED Pin
#define LED_PIN 21        // Change to your board's LED pin (Note: if S3-Zero uses WS2812 RGB, this will just toggle logic level, not color)

// --- CONFIG ---
const char* SSID = "S3-Zero-Remote";
const char* PASSWORD = ""; 

// --- DATA STRUCTURES ---
struct StoredCode {
    uint16_t protocol;
    uint16_t address;
    uint32_t command;
    uint8_t bits;
    uint8_t repeats;
};

struct IrQueueData {
    uint16_t protocol;
    uint16_t address;
    uint32_t command;
    uint8_t bits;
    uint8_t repeats;
};

// --- GLOBALS ---
WebRemote webRemote(80);
Preferences prefs;
StoredCode memory[14]; 
String channels[5] = {"", "", "", "", ""};
QueueHandle_t irQueue;

volatile uint32_t lastCode = 0;
volatile uint16_t lastProtocol = 0;
int learningIndex = -1;

// --- JSON DATA PROVIDER ---
String getMemoryJSON() {
    String json = "{\"mem\":[";
    String bitsJson = "], \"bits\":[";
    
    for (int i = 0; i < 14; i++) {
        json += "\"" + String(memory[i].command, HEX) + "\"";
        bitsJson += String(memory[i].bits);
        if (i < 13) {
            json += ",";
            bitsJson += ",";
        }
    }
    
    json += bitsJson + "], \"channels\":[";
    for (int i = 0; i < 5; i++) {
        json += "\"" + channels[i] + "\"";
        if (i < 4) json += ",";
    }
    json += "]}";
    return json;
}

// --- QUEUE IR SIGNAL ---
void queueIR(int index) {
    if (index < 0 || index >= 14 || memory[index].protocol == 0) {
        Serial.println("Empty or invalid slot.");
        return;
    }

    IrQueueData* txData = new IrQueueData;
    txData->protocol = memory[index].protocol;
    txData->address = memory[index].address;
    txData->command = memory[index].command;
    txData->bits = memory[index].bits;
    
    // Check if it's the Power button (index 10) and it's a Sony TV
    if (index == 10 && txData->protocol == SONY) {
        txData->repeats = 4; // Power boost!
        Serial.println("Applying Sony Power Boost (4 repeats)");
    } else {
        txData->repeats = memory[index].repeats;
    }

    xQueueSend(irQueue, &txData, portMAX_DELAY);
}

// --- IR SENDER TASK (WITH LED PULSE) ---
void irCoreTask(void * parameter) {
    IrQueueData* txData;
    
    for(;;) {
        if (xQueueReceive(irQueue, &txData, 0) == pdTRUE) {
            IrReceiver.stop(); 
            
            digitalWrite(LED_PIN, HIGH); // LED ON
            
            // Map the internal queue data to the library's required struct
            IRData sendData;
            sendData.protocol = (decode_type_t)txData->protocol;
            sendData.address = txData->address;
            sendData.command = txData->command;
            sendData.numberOfBits = txData->bits;
            sendData.flags = 0;

            IrSender.write(&sendData, txData->repeats); 
            
            digitalWrite(LED_PIN, LOW); // LED OFF
            
            delete txData; 
            IrReceiver.start(); 
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// --- WEB COMMAND HANDLER ---
void handleWebCommand(String cmd) {
    if (cmd.startsWith("learn-")) {
        learningIndex = cmd.substring(6).toInt();
        Serial.printf("Ready to learn index %d\n", learningIndex);
    } 
    else if (cmd == "power") queueIR(10);
    else if (cmd == "source") queueIR(11);
    else if (cmd == "volup") queueIR(12);
    else if (cmd == "voldown") queueIR(13);
    else if (cmd.startsWith("setchan-")) {
        // Parse setchan-1-123
        int dash1 = cmd.indexOf('-');
        int dash2 = cmd.indexOf('-', dash1 + 1);
        if(dash1 > 0 && dash2 > 0) {
            int chIdx = cmd.substring(dash1 + 1, dash2).toInt() - 1;
            String val = cmd.substring(dash2 + 1);
            if(chIdx >= 0 && chIdx < 5) {
                channels[chIdx] = val;
                // Save to NVS
                prefs.putString(("ch" + String(chIdx)).c_str(), val);
            }
        }
    }
    // Add logic for "send-pre-X" here to queue the digits based on the string
}

// --- SETUP ---
void setup() {
    Serial.begin(115200);
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Initialize IR
    IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
    IrSender.begin(IR_SEND_PIN);
    
    irQueue = xQueueCreate(10, sizeof(IrQueueData*));
    xTaskCreatePinnedToCore(irCoreTask, "IR_Task", 4096, NULL, 1, NULL, 1);

    // Load Memory
    prefs.begin("remote", false);
    if (prefs.getBytesLength("mem") == sizeof(memory)) {
        prefs.getBytes("mem", memory, sizeof(memory));
    } else {
        memset(memory, 0, sizeof(memory));
    }
    for(int i=0; i<5; i++) {
        channels[i] = prefs.getString(("ch" + String(i)).c_str(), "");
    }

    // Initialize Web
    webRemote.begin(SSID, PASSWORD);
    webRemote.setCallback(handleWebCommand);
    webRemote.setDataProvider(getMemoryJSON);
    
    // Status Provider
    webRemote.setStatusProvider([]() -> String {
        if (learningIndex >= 0) {
            return "System: Recording index " + String(learningIndex) + "... Press Remote Button";
        }
        if (lastCode == 0) return "Waiting for signal...";
        
        return "Protocol: " + String(lastProtocol) + 
               " | Bits: " + String(IrReceiver.decodedIRData.numberOfBits) +
               "<br>Code: 0x" + String(lastCode, HEX);
    });
}

// --- LOOP (LEARNING) ---
void loop() {
    webRemote.handle();

    if (IrReceiver.decode()) {
        if (IrReceiver.decodedIRData.protocol != UNKNOWN) {
            lastCode = IrReceiver.decodedIRData.command;
            lastProtocol = IrReceiver.decodedIRData.protocol;
            
            // If we are actively learning
            if (learningIndex >= 0 && learningIndex < 14) {
                memory[learningIndex].protocol = IrReceiver.decodedIRData.protocol;
                memory[learningIndex].address = IrReceiver.decodedIRData.address;
                memory[learningIndex].command = IrReceiver.decodedIRData.command;
                memory[learningIndex].bits = IrReceiver.decodedIRData.numberOfBits;
                
                // Smart Repeats
                memory[learningIndex].repeats = (IrReceiver.decodedIRData.protocol == SONY) ? 2 : 0;
                
                prefs.putBytes("mem", memory, sizeof(memory));
                Serial.printf("Learned index %d: 0x%X\n", learningIndex, lastCode);
                
                learningIndex = -1; // End learning mode
            }
        }
        IrReceiver.resume(); 
    }
}