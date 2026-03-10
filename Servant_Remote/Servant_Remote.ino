#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h> 

#define POW_PIN 22
#define CH1_PIN 14
#define CH2_PIN 13
#define CH3_PIN 12
#define VOL_UP_PIN 11
#define VOL_DOWN_PIN 10

// --- UUIDs (MUST MATCH BLASTER) ---
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

BLEScan* pBLEScan;
String commandToSend = ""; 

// --- Interrupt Variables ---
// 'volatile' tells the compiler these change inside an interrupt
volatile bool buttonFlag = false;
volatile int activeButtonPin = -1;
volatile unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 250; // 250ms ignore window for bouncy buttons

// --- Interrupt Service Routines (ISRs) ---
void IRAM_ATTR isr_pow()      { if (millis() - lastDebounceTime > debounceDelay) { activeButtonPin = POW_PIN;      buttonFlag = true; lastDebounceTime = millis(); } }
void IRAM_ATTR isr_ch1()      { if (millis() - lastDebounceTime > debounceDelay) { activeButtonPin = CH1_PIN;      buttonFlag = true; lastDebounceTime = millis(); } }
void IRAM_ATTR isr_ch2()      { if (millis() - lastDebounceTime > debounceDelay) { activeButtonPin = CH2_PIN;      buttonFlag = true; lastDebounceTime = millis(); } }
void IRAM_ATTR isr_ch3()      { if (millis() - lastDebounceTime > debounceDelay) { activeButtonPin = CH3_PIN;      buttonFlag = true; lastDebounceTime = millis(); } }
void IRAM_ATTR isr_vol_up()   { if (millis() - lastDebounceTime > debounceDelay) { activeButtonPin = VOL_UP_PIN;   buttonFlag = true; lastDebounceTime = millis(); } }
void IRAM_ATTR isr_vol_down() { if (millis() - lastDebounceTime > debounceDelay) { activeButtonPin = VOL_DOWN_PIN; buttonFlag = true; lastDebounceTime = millis(); } }

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Client (H2 Mini)...");

  BLEDevice::init("H2-Remote");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  // Set all pins as inputs with pullups
  pinMode(POW_PIN, INPUT_PULLUP);
  pinMode(CH1_PIN, INPUT_PULLUP);
  pinMode(CH2_PIN, INPUT_PULLUP);
  pinMode(CH3_PIN, INPUT_PULLUP);
  pinMode(VOL_UP_PIN, INPUT_PULLUP);
  pinMode(VOL_DOWN_PIN, INPUT_PULLUP);

  // Attach distinct ISRs, trigger on FALLING edge (when pressed to ground)
  attachInterrupt(digitalPinToInterrupt(POW_PIN), isr_pow, FALLING);
  attachInterrupt(digitalPinToInterrupt(CH1_PIN), isr_ch1, FALLING);
  attachInterrupt(digitalPinToInterrupt(CH2_PIN), isr_ch2, FALLING);
  attachInterrupt(digitalPinToInterrupt(CH3_PIN), isr_ch3, FALLING);
  attachInterrupt(digitalPinToInterrupt(VOL_UP_PIN), isr_vol_up, FALLING);
  attachInterrupt(digitalPinToInterrupt(VOL_DOWN_PIN), isr_vol_down, FALLING);
}

bool connectAndSend(BLEAddress pAddress) {
    BLEClient* pClient = BLEDevice::createClient();
    Serial.print(" - Connecting to: "); Serial.println(pAddress.toString().c_str());

    if (!pClient->connect(pAddress)) {
        Serial.println(" - Connection Failed");
        delete pClient;
        return false;
    }

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println(" - Failed to find Service UUID");
        pClient->disconnect();
        delete pClient;
        return false;
    }

    BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.println(" - Failed to find Characteristic UUID");
        pClient->disconnect();
        delete pClient;
        return false;
    }

    Serial.print(" - Writing: "); Serial.println(commandToSend);
    pRemoteCharacteristic->writeValue(commandToSend.c_str(), commandToSend.length());

    pClient->disconnect();
    Serial.println(" - Disconnected");
    
    delete pClient; 
    return true;
}

void loop() {
  // Main loop safely handles the heavy BLE lifting if a button was pressed
  if (buttonFlag) {
      buttonFlag = false; // Reset flag immediately
      
      // Determine which command to send based on the pin saved by the ISR
      switch (activeButtonPin) {
          case POW_PIN:      commandToSend = "power"; break;
          case CH1_PIN:      commandToSend = "ch1"; break;
          case CH2_PIN:      commandToSend = "ch2"; break;
          case CH3_PIN:      commandToSend = "ch3"; break;
          case VOL_UP_PIN:   commandToSend = "vol_up"; break;
          case VOL_DOWN_PIN: commandToSend = "vol_down"; break;
      }

      Serial.print("\nButton pressed! Target command: ");
      Serial.println(commandToSend);
      Serial.println("Scanning...");

      // Perform the 3-second scan
      BLEScanResults *foundDevices = pBLEScan->start(3, false);
      BLEAddress *targetAddress = nullptr;
      
      for (int i = 0; i < foundDevices->getCount(); i++) {
          BLEAdvertisedDevice device = foundDevices->getDevice(i);
          if (device.haveServiceUUID() && device.isAdvertisingService(serviceUUID)) {
              Serial.println("Found S3 Receiver!");
              targetAddress = new BLEAddress(device.getAddress());
              break; 
          }
      }

      pBLEScan->clearResults(); 

      if (targetAddress != nullptr) {
          connectAndSend(*targetAddress);
          delete targetAddress; 
      } else {
          Serial.println("S3 Receiver not found.");
      }
  }
}