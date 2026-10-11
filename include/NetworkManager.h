#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"

class NetworkManager {
private:
    String apSSID;
    String apPass;
    String staSSID;
    String staPass;

    bool staConnected = false;
    unsigned long lastReconnectAttempt = 0;
    const unsigned long reconnectInterval = 30000; // 30 seconds

public:
    NetworkManager() {}

    void begin(const DeviceSettings& settings) {
        apSSID = String(settings.apSSID);
        apPass = String(settings.apPass);
        staSSID = String(settings.staSSID);
        staPass = String(settings.staPass);

        // Configure Dual Mode: Access Point + Station
        WiFi.mode(WIFI_AP_STA);

        // Configure and start Access Point
        IPAddress apIP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, gateway, subnet);
        
        if (apPass.length() > 0) {
            WiFi.softAP(apSSID.c_str(), apPass.c_str());
        } else {
            WiFi.softAP(apSSID.c_str());
        }

        Serial.printf("[WIFI] Access Point '%s' started on IP: %s\n",
                      apSSID.c_str(), WiFi.softAPIP().toString().c_str());

        // Connect to Station if credentials provided
        if (staSSID.length() > 0) {
            connectStation();
        } else {
            Serial.println("[WIFI] No Station SSID configured. Running in AP setup mode.");
        }
    }

    void connectStation() {
        if (staSSID.length() == 0) return;
        Serial.printf("[WIFI] Connecting to Internet Station '%s'...\n", staSSID.c_str());
        WiFi.begin(staSSID.c_str(), staPass.c_str());
        lastReconnectAttempt = millis();
    }

    void update() {
        bool currentStaStatus = (WiFi.status() == WL_CONNECTED);
        if (currentStaStatus != staConnected) {
            staConnected = currentStaStatus;
            if (staConnected) {
                Serial.printf("[WIFI] Station Connected! IP: %s, RSSI: %d dBm\n",
                              WiFi.localIP().toString().c_str(), WiFi.RSSI());
            } else {
                Serial.println("[WIFI] Station Disconnected.");
            }
        }

        // Automatic non-blocking reconnection for Station mode
        if (!staConnected && staSSID.length() > 0) {
            if (millis() - lastReconnectAttempt > reconnectInterval) {
                Serial.println("[WIFI] Re-attempting Station connection...");
                WiFi.begin(staSSID.c_str(), staPass.c_str());
                lastReconnectAttempt = millis();
            }
        }
    }

    bool isConnectedToInternet() const {
        return WiFi.status() == WL_CONNECTED;
    }

    String getApIP() const {
        return WiFi.softAPIP().toString();
    }

    String getStaIP() const {
        return isConnectedToInternet() ? WiFi.localIP().toString() : "Not connected";
    }

    int getRSSI() const {
        return isConnectedToInternet() ? WiFi.RSSI() : 0;
    }

    String getMacAddress() const {
        return WiFi.macAddress();
    }

    void setCredentials(const String& newSSID, const String& newPass) {
        staSSID = newSSID;
        staPass = newPass;
        connectStation();
    }
};

#endif // NETWORK_MANAGER_H
