#include <Arduino.h>
#include <IRremote.hpp>
#include <WiFi.h> 
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "WebRemote.h"

///////////////////////////////////////////////
///               CONFIG                    ///
///////////////////////////////////////////////

const uint16_t RECV_PIN = 13;
const uint16_t SEND_PIN = 12;

#define SSID "S3-Zero-Remote"
#define PASSWORD "password123"

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

///////////////////////////////////////////////
///               GLOBALS                   ///
///////////////////////////////////////////////

struct StoredCode {
    uint16_t protocol;
    uint16_t address;
    uint32_t command;
};

StoredCode memory[14]; 
int channelPresets[5] = {0, 0, 0, 0, 0};
volatile int learningIndex = -1;

// For Web Display
volatile uint32_t lastCode = 0;
volatile uint16_t lastProtocol = 0;

Preferences prefs;
WebRemote webRemote(80);
QueueHandle_t irQueue;

// Forward Declarations
void queueIR(StoredCode sc);
void processCommand(String cmd);

///////////////////////////////////////////////
///            MEMORY SYSTEM                ///
///////////////////////////////////////////////

void saveCode(int idx, IRData *data) {
    prefs.begin("ir-v2", false);
    String p = "i" + String(idx);
    prefs.putShort((p+"t").c_str(), (int16_t)data->protocol);
    prefs.putShort((p+"a").c_str(), (int16_t)data->address);
    prefs.putUInt((p+"c").c_str(), data->command);
    prefs.end();
    
    memory[idx] = {(uint16_t)data->protocol, data->address, data->command};
}

void loadMemory() {
    prefs.begin("ir-v2", true);
    for(int i=0; i<14; i++) {
        String p = "i" + String(i);
        memory[i].protocol = prefs.getShort((p+"t").c_str(), 0);
        memory[i].address = prefs.getShort((p+"a").c_str(), 0);
        memory[i].command = prefs.getUInt((p+"c").c_str(), 0);
    }
    prefs.end();
    
    prefs.begin("ch-presets", true);
    for(int i=0; i<5; i++) {
        channelPresets[i] = prefs.getInt(String(i).c_str(), 0);
    }
    prefs.end();
}

String getMemoryJSON() {
    String json = "{ \"mem\": [";
    for (int i = 0; i < 14; i++) {
        if (memory[i].protocol == 0) {
            json += "\"0\""; 
        } else {
            char hexBuffer[12];
            sprintf(hexBuffer, "\"0x%08X\"", memory[i].command);
            json += String(hexBuffer);
        }
        if (i < 13) json += ",";
    }
    json += "], \"channels\": [";
    for (int i = 0; i < 5; i++) {
        json += String(channelPresets[i]);
        if (i < 4) json += ",";
    }
    json += "] }";
    return json;
}

///////////////////////////////////////////////
///            IR SEND LOGIC                ///
///////////////////////////////////////////////

void queueIR(StoredCode sc) {
    if (sc.protocol == 0) return;
    IRData* data = new IRData();
    data->protocol = (decode_type_t)sc.protocol;
    data->address = sc.address;
    data->command = sc.command;
    data->flags = IRDATA_FLAGS_EMPTY;
    xQueueSend(irQueue, &data, portMAX_DELAY);
}

void sendChannelDigits(int channel) {
    if (channel <= 0) return;
    String s = String(channel);
    for (char const &c : s) {
        int digit = c - '0';
        if (digit >= 0 && digit <= 9) {
            queueIR(memory[digit]);
            delay(400); // Wait for TV to register digit
        }
    }
}

///////////////////////////////////////////////
///          COMMAND PROCESSING             ///
///////////////////////////////////////////////

void processCommand(String cmd) {
    cmd.trim();
    if (cmd.equalsIgnoreCase("power"))      queueIR(memory[10]);
    else if (cmd.equalsIgnoreCase("source"))  queueIR(memory[11]);
    else if (cmd.equalsIgnoreCase("volup"))   queueIR(memory[12]);
    else if (cmd.equalsIgnoreCase("voldown")) queueIR(memory[13]);
    else {
        Serial.println("Unknown Command: " + cmd);
    }
}

void handleWebCommand(String cmd) {
    if (cmd.startsWith("learn-")) {
        learningIndex = cmd.substring(6).toInt();
    } 
    else if (cmd.startsWith("setchan-")) {
        int firstDash = cmd.indexOf('-');
        int lastDash = cmd.lastIndexOf('-');
        int idx = cmd.substring(firstDash + 1, lastDash).toInt() - 1;
        int val = cmd.substring(lastDash + 1).toInt();
        
        if (idx >= 0 && idx < 5) {
            channelPresets[idx] = val;
            prefs.begin("ch-presets", false);
            prefs.putInt(String(idx).c_str(), val);
            prefs.end();
        }
    }
    else if (cmd.startsWith("send-pre-")) {
        int idx = cmd.substring(9).toInt() - 1;
        sendChannelDigits(channelPresets[idx]);
    }
    else {
        processCommand(cmd); 
    }
}

///////////////////////////////////////////////
///            BLUETOOTH (BLE)              ///
///////////////////////////////////////////////

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Serial.println("BLE Device Connected");
    };
    void onDisconnect(BLEServer* pServer) {
      Serial.println("BLE Device Disconnected - Restarting Adverts");
      BLEDevice::startAdvertising(); 
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue(); 
      if (value.length() > 0) {
        Serial.print("BLE RX: "); Serial.println(value);
        processCommand(value); 
      }
    }
};

///////////////////////////////////////////////
///             IR TASK                     ///
///////////////////////////////////////////////

void irCoreTask(void *pvParameters) {
    IrReceiver.begin(RECV_PIN, ENABLE_LED_FEEDBACK);
    IrSender.begin(SEND_PIN);
    IRData* txData;

    for (;;) {

        if (IrReceiver.decode()) {
            // Must be valid protocol and NOT 0
            bool isValid = (IrReceiver.decodedIRData.protocol != UNKNOWN) && 
                           (IrReceiver.decodedIRData.decodedRawData != 0);
            
            // Ignore repeat codes for learning
            bool isRepeat = (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT);

            if (isValid && !isRepeat) {
                if (learningIndex != -1) {
                    Serial.printf("Learned[%d]: 0x%08X\n", learningIndex, IrReceiver.decodedIRData.decodedRawData);
                    saveCode(learningIndex, &IrReceiver.decodedIRData);
                    learningIndex = -1; 
                } else {
                    lastProtocol = IrReceiver.decodedIRData.protocol;
                    lastCode = IrReceiver.decodedIRData.decodedRawData;
                }
            }
            IrReceiver.resume();
        }


        if (xQueueReceive(irQueue, &txData, 0) == pdTRUE) {
            IrReceiver.stop(); // Stop RX to prevent self-feedback
            IrSender.write(txData, 0); // Generic Write (handles all protocols)
            delete txData; 
            IrReceiver.start(); // Restart RX
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

///////////////////////////////////////////////
///               SETUP                     ///
///////////////////////////////////////////////

void setup() {
    Serial.begin(115200);
    irQueue = xQueueCreate(10, sizeof(IRData*));
    loadMemory();

  
    BLEDevice::init("S3-Receiver");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    pCharacteristic->setCallbacks(new MyCallbacks()); 
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();


    webRemote.begin(SSID, PASSWORD);
    webRemote.setCallback(handleWebCommand);
    webRemote.setDataProvider(getMemoryJSON);
    
 
    xTaskCreatePinnedToCore(irCoreTask, "IR_Task", 4096, NULL, 1, NULL, 1);
    
    Serial.println("System Ready: BLE + Web + IR Learning");
}

void loop() {
    webRemote.handle();
    delay(2);
}