#ifndef WEBREMOTE_H
#define WEBREMOTE_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h> // Captive Portal
#include <functional>

extern volatile uint32_t lastCode;
extern volatile uint16_t lastProtocol;

typedef std::function<void(String)> CommandCallback;

class WebRemote {
    public:
        WebRemote(int port);
        void begin(const char* ssid, const char* password);
        void handle(); 
        void setCallback(CommandCallback cb);

    private:
        WebServer server;
        DNSServer dnsServer; 
        CommandCallback onCommand;
        
        void handleRoot();
        void handleSend();
        void handleStatus();
        void handleNotFound(); 
};

#endif