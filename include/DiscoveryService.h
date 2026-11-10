#ifndef DISCOVERY_SERVICE_H
#define DISCOVERY_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "Config.h"

// Answers UDP discovery probes from the desktop admin app.
// Probe:  ASCII "SGW_DISCOVER" sent to the broadcast address on DISCOVERY_UDP_PORT
// Reply:  JSON unicast back to the probe sender, containing only non-secret identity data.
class DiscoveryService {
private:
    WiFiUDP udp;
    bool running = false;
    const DeviceSettings* settings = nullptr;

public:
    void begin(const DeviceSettings* cfg) {
        settings = cfg;
        running = udp.begin(DISCOVERY_UDP_PORT) == 1;
        Serial.printf("[DISCOVERY] UDP responder on port %d: %s\n",
                      DISCOVERY_UDP_PORT, running ? "OK" : "FAILED");
    }

    void update() {
        if (!running) return;
        int size = udp.parsePacket();
        if (size <= 0) return;

        char buf[32];
        int n = udp.read((uint8_t*)buf, sizeof(buf) - 1);
        if (n <= 0) return;
        buf[n] = '\0';

        IPAddress remoteIP = udp.remoteIP();
        uint16_t remotePort = udp.remotePort();

        if (strncmp(buf, "SGW_DISCOVER", 12) != 0) return;

        JsonDocument doc;
        doc["sgw"] = 1;
        doc["name"] = settings->deviceName;
        doc["fw"] = FIRMWARE_VERSION;
        doc["mac"] = WiFi.macAddress();
        doc["port"] = ADMIN_API_PORT;
        doc["serviceEnabled"] = settings->serviceEnabled;
        doc["staIP"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
        doc["apIP"] = WiFi.softAPIP().toString();

        String reply;
        serializeJson(doc, reply);

        udp.beginPacket(remoteIP, remotePort);
        udp.write((const uint8_t*)reply.c_str(), reply.length());
        udp.endPacket();
    }
};

#endif // DISCOVERY_SERVICE_H
