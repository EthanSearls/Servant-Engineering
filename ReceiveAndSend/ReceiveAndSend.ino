
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "PinDefinitionsAndMore.h" // Define macros for input and output pin etc.

/*
 * Specify which protocol(s) should be used for decoding.
 * If no protocol is defined, all protocols (except Bang&Olufsen) are active.
 * This must be done before the #include <IRremote.hpp>
 */
//#define DECODE_DENON        // Includes Sharp
//#define DECODE_JVC
//#define DECODE_KASEIKYO
//#define DECODE_PANASONIC    // alias for DECODE_KASEIKYO
//#define DECODE_LG
//#define DECODE_NEC          // Includes Apple and Onkyo
//#define DECODE_SAMSUNG
//#define DECODE_SONY
//#define DECODE_RC5
//#define DECODE_RC6

//#define DECODE_BOSEWAVE
//#define DECODE_LEGO_PF
//#define DECODE_MAGIQUEST
//#define DECODE_WHYNTER
//#define DECODE_FAST
//

#if !defined(RAW_BUFFER_LENGTH)
// For air condition remotes it may require up to 750. Default is 200.
#  if !((defined(RAMEND) && RAMEND <= 0x4FF) || (defined(RAMSIZE) && RAMSIZE < 0x4FF))
#define RAW_BUFFER_LENGTH  700 // we require 2 buffer of this size for this example
#  endif
#endif

//#define EXCLUDE_UNIVERSAL_PROTOCOLS // Saves up to 1000 bytes program memory.
//#define EXCLUDE_EXOTIC_PROTOCOLS // saves around 650 bytes program memory if all other protocols are active
//#define NO_LED_FEEDBACK_CODE      // saves 92 bytes program memory
#define RECORD_GAP_MICROS 12000   // Default is 8000. Activate it for some LG air conditioner protocols
//#define SEND_PWM_BY_TIMER         // Disable carrier PWM generation in software and use (restricted) hardware PWM.
//#define USE_NO_SEND_PWM           // Use no carrier PWM, just simulate an active low receiver signal. Overrides SEND_PWM_BY_TIMER definition
//#define NO_LED_FEEDBACK_CODE      // Saves 202 bytes program memory

// MARK_EXCESS_MICROS is subtracted from all marks and added to all spaces before decoding,
// to compensate for the signal forming of different IR receiver modules. See also IRremote.hpp line 135.
// 20 is taken as default if not otherwise specified / defined.
//#define MARK_EXCESS_MICROS    40    // Adapt it to your IR receiver module. 40 is recommended for the cheap VS1838 modules at high intensity.

//#define DEBUG // Activate this for lots of lovely debug output from the decoders.

#include <IRremote.hpp>



int SEND_BUTTON_PIN = APPLICATION_PIN;

int DELAY_BETWEEN_REPEAT = 50;
uint8_t incomingCommand;
// Storage for the recorded code
struct storedIRDataStruct {
    IRData receivedIRData;
    // extensions for sendRaw
    uint8_t rawCode[RAW_BUFFER_LENGTH]; // The durations if raw
    uint8_t rawCodeLength; // The length of the code
} sStoredIRData;

bool sSendButtonWasActive;
volatile bool send_code = false;
void storeCode();
void sendCode(storedIRDataStruct *aIRDataToSend);

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len >= 1) {
    const char* command;
    incomingCommand = incomingData[0];
    switch (incomingCommand) {
      case 0x01:
        command = "POWER_ON";
        break;
      case 0x02:
        command = "POWER_OFF";
        break;
      case 0x03:
        command = "VOLUME_UP";
        break;
      case 0x04:
        command = "VOLUME_DOWN";
        break;
      case 0x05:
        command = "CHANNEL_UP";
        break;
      case 0x06:
        command = "CHANNEL_DOWN";
        break;
      default:
        command = "unknown";
        break;
    }
    const char* label = command;

    Serial.print("ESP-NOW command received: ");
    Serial.println(label);
    send_code = true;
  }
}

void setup() {
    pinMode(SEND_BUTTON_PIN, INPUT_PULLUP);

    Serial.begin(115200);
    delay(1000);
    WiFi.mode(WIFI_STA);
    Serial.print("ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());
    // Just to know which program is running on my Arduino
    Serial.println(F("START " __FILE__ " from " __DATE__ "\r\nUsing library version " VERSION_IRREMOTE));

    // Start the receiver and if not 3. parameter specified, take LED_BUILTIN pin from the internal boards definition as default feedback LED
    IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
    Serial.print(F("Ready to receive IR signals of protocols: "));
    printActiveIRProtocols(&Serial);
    Serial.println(F("at pin " STR(IR_RECEIVE_PIN)));

if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
    /*
     * No IR library setup required :-)
     * Default is to use IR_SEND_PIN -which is defined in PinDefinitionsAndMore.h- as send pin
     * and use feedback LED at default feedback LED pin if not disabled by #define NO_LED_SEND_FEEDBACK_CODE
     */
    Serial.print(F("Ready to send IR signal (with repeats) at pin " STR(IR_SEND_PIN) " as long as button at pin "));
    Serial.print(SEND_BUTTON_PIN);
    Serial.println(F(" is pressed."));
    esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW receiver ready.");
}

void loop() {

    // If button pressed, send the code.
    bool tSendButtonIsActive = (digitalRead(SEND_BUTTON_PIN) == LOW); // Button pin is active LOW
    if (send_code) {
      tSendButtonIsActive = true;
      send_code = false;
    }

    /*
     * Check for current button state
     */
    if (tSendButtonIsActive) {
        if (!sSendButtonWasActive) {
            Serial.println(F("Stop receiving"));
            IrReceiver.stop();
        }
        /*
         * Button pressed -> send stored data
         */
        Serial.print(F("Button pressed, now sending "));
        if (sSendButtonWasActive == tSendButtonIsActive) {
            Serial.print(F("repeat "));
            sStoredIRData.receivedIRData.flags = IRDATA_FLAGS_IS_REPEAT;
        } else {
            sStoredIRData.receivedIRData.flags = IRDATA_FLAGS_EMPTY;
        }
        Serial.flush(); // To avoid disturbing the software PWM generation by serial output interrupts
        sendCode(&sStoredIRData);
        delay(DELAY_BETWEEN_REPEAT); // Wait a bit between retransmissions

    } else if (sSendButtonWasActive) {
        /*
         * Button is just released -> activate receiving
         */
        // Restart receiver
        Serial.println(F("Button released -> start receiving"));
        IrReceiver.start();
        delay(100); // Button debouncing

    } else if (IrReceiver.decode()) {
        /*
         * Button is not pressed and data available -> store received data and resume
         */
        storeCode();
        IrReceiver.resume(); // resume receiver
    }

    sSendButtonWasActive = tSendButtonIsActive;
}

// Stores the code for later playback in sStoredIRData
// Most of this code is just logging
void storeCode() {
    if (IrReceiver.irparams.rawlen < 4) {
        Serial.print(F("Ignore data with rawlen="));
        Serial.println(IrReceiver.irparams.rawlen);
        return;
    }
    if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) {
        Serial.println(F("Ignore repeat"));
        return;
    }
    if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_AUTO_REPEAT) {
        Serial.println(F("Ignore autorepeat"));
        return;
    }
    if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_PARITY_FAILED) {
        Serial.println(F("Ignore parity error"));
        return;
    }
    if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_WAS_OVERFLOW) {
        Serial.println(F("Overflow occurred, raw data did not fit into " STR(RAW_BUFFER_LENGTH) " byte raw buffer"));
        return;
    }
    /*
     * Copy decoded data
     */
    sStoredIRData.receivedIRData = IrReceiver.decodedIRData;

    auto tProtocol = sStoredIRData.receivedIRData.protocol;
    if (tProtocol == UNKNOWN || tProtocol == PULSE_WIDTH || tProtocol == PULSE_DISTANCE) {
        // TODO: support PULSE_WIDTH and PULSE_DISTANCE with IrSender.write
        sStoredIRData.rawCodeLength = IrReceiver.irparams.rawlen - 1;
        /*
         * Store the current raw data in a dedicated array for later usage
         */
        IrReceiver.compensateAndStoreIRResultInArray(sStoredIRData.rawCode);
        /*
         * Print info
         */
        Serial.print(F("Received unknown or pulse width/distance code and store "));
        Serial.print(IrReceiver.irparams.rawlen - 1);
        Serial.println(F(" timing entries as raw in buffer of size " STR(RAW_BUFFER_LENGTH)));
        IrReceiver.printIRResultRawFormatted(&Serial, true); // Output the results in RAW format

    } else {
        IrReceiver.printIRResultShort(&Serial);
        IrReceiver.printIRSendUsage(&Serial);
        sStoredIRData.receivedIRData.flags = 0; // clear flags -esp. repeat- for later sending
        Serial.println();
    }
}

void sendCode(storedIRDataStruct *aIRDataToSend) {
    auto tProtocol = aIRDataToSend->receivedIRData.protocol;
    if (tProtocol == UNKNOWN || tProtocol == PULSE_WIDTH || tProtocol == PULSE_DISTANCE /* i.e. raw */) {
        // Assume 38 KHz
        IrSender.sendRaw(aIRDataToSend->rawCode, aIRDataToSend->rawCodeLength, 38);

        Serial.print(F("raw "));
        Serial.print(aIRDataToSend->rawCodeLength);
        Serial.println(F(" marks or spaces"));
    } else {
        /*
         * Use the write function, which does the switch for different protocols
         */
        IrSender.write(&aIRDataToSend->receivedIRData);
        printIRDataShort(&Serial, &aIRDataToSend->receivedIRData);
    }
}

