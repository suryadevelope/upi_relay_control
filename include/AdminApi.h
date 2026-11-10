#ifndef ADMIN_API_H
#define ADMIN_API_H

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "Config.h"
#include "StorageManager.h"
#include "RelayController.h"
#include "NetworkManager.h"
#include "SecurityUtils.h"

// Live runtime values written by main.cpp and read by the API (no secrets)
struct RuntimeStatus {
    const char* state = "STARTUP";
    bool relayOn = false;
    uint32_t relayRemainingSec = 0;
    bool paymentActive = false;
    String activeQrId = "";
    float activeAmount = 0.0f;
    uint32_t activeDuration = 0;
};

// Actions the API asks main.cpp to perform (main owns the state machine)
struct AdminCallbacks {
    std::function<bool(float, uint32_t, String&)> startPayment;
    std::function<void()> cancelPayment;
    std::function<void(bool)> serviceChanged;
    std::function<bool(uint32_t, String&)> relayTest;
    std::function<void()> relayOff;
    std::function<void()> configChanged;
};

// JSON admin API for the desktop app. Served on LAN and hotspot, never serves HTML.
// Auth: HMAC challenge-response (password never sent) -> bearer session token.
class AdminApi {
private:
    WebServer server{ADMIN_API_PORT};
    StorageManager* storage = nullptr;
    RelayController* relay = nullptr;
    NetworkManager* network = nullptr;
    const RuntimeStatus* runtime = nullptr;
    AdminCallbacks cb;

    String sessionToken = "";
    unsigned long sessionLastSeen = 0;

    String challengeNonce = "";
    unsigned long challengeIssued = 0;
    static const unsigned long CHALLENGE_TTL_MS = 60UL * 1000UL;

    int failedLogins = 0;
    unsigned long lockedUntil = 0;

    // ---------- response helpers ----------
    void sendJson(int code, JsonDocument& doc) {
        String out;
        serializeJson(doc, out);
        server.send(code, "application/json", out);
    }

    void sendError(int code, const String& message) {
        JsonDocument doc;
        doc["success"] = false;
        doc["error"] = message;
        sendJson(code, doc);
    }

    void sendOk() {
        JsonDocument doc;
        doc["success"] = true;
        sendJson(200, doc);
    }

    // Parses the JSON body; sends 400 and returns false on failure
    bool readBody(JsonDocument& doc) {
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            sendError(400, "Body must be valid JSON");
            return false;
        }
        return true;
    }

    // ---------- auth ----------
    bool isLocked() const {
        return lockedUntil != 0 && millis() < lockedUntil;
    }

    bool authorize() {
        if (sessionToken.length() == 0) return false;
        if (millis() - sessionLastSeen > ADMIN_SESSION_IDLE_MS) {
            Serial.println("[API] Admin session expired (idle).");
            sessionToken = "";
            return false;
        }
        String header = server.header("Authorization");
        if (!header.startsWith("Bearer ")) return false;
        String token = header.substring(7);
        token.trim();
        if (!SecurityUtils::constantTimeEquals(token, sessionToken)) return false;
        sessionLastSeen = millis();
        return true;
    }

    // Returns true when the request is authorized; otherwise sends the error response
    bool requireAuth() {
        if (authorize()) return true;
        sendError(401, "Login required");
        return false;
    }

    bool requireHotspot() {
        if (SecurityUtils::requestFromHotspot(server)) return true;
        sendError(403, "Wi-Fi settings can only be changed from the device hotspot");
        return false;
    }

    bool defaultCredentialsInUse() {
        return strcmp(storage->getSettings().adminPass, DEFAULT_ADMIN_PASS) == 0;
    }

    // ---------- handlers ----------
    void handleInfo() {
        DeviceSettings& s = storage->getSettings();
        JsonDocument doc;
        doc["sgw"] = 1;
        doc["name"] = s.deviceName;
        doc["fw"] = FIRMWARE_VERSION;
        doc["mac"] = WiFi.macAddress();
        doc["port"] = ADMIN_API_PORT;
        doc["serviceEnabled"] = s.serviceEnabled;
        doc["staConnected"] = network->isConnectedToInternet();
        doc["staIP"] = network->getStaIP();
        doc["apIP"] = network->getApIP();
        sendJson(200, doc);
    }

    void handleChallenge() {
        if (isLocked()) {
            sendError(429, "Too many failed logins. Try again later.");
            return;
        }
        challengeNonce = SecurityUtils::randomHex(16);
        challengeIssued = millis();
        JsonDocument doc;
        doc["nonce"] = challengeNonce;
        doc["expiresInSec"] = CHALLENGE_TTL_MS / 1000;
        sendJson(200, doc);
    }

    void handleLogin() {
        if (isLocked()) {
            sendError(429, "Too many failed logins. Try again later.");
            return;
        }
        JsonDocument body;
        if (!readBody(body)) return;

        String user = body["user"].as<String>();
        String proof = body["proof"].as<String>();

        // The nonce is single-use: consume it whatever the outcome
        String nonce = challengeNonce;
        bool nonceFresh = nonce.length() > 0 && (millis() - challengeIssued) <= CHALLENGE_TTL_MS;
        challengeNonce = "";

        DeviceSettings& s = storage->getSettings();
        bool ok = false;
        if (nonceFresh && user == String(s.adminUser)) {
            String expected = SecurityUtils::hmacSha256Hex(String(s.adminPass), user + ":" + nonce);
            ok = SecurityUtils::constantTimeEquals(proof, expected);
        }

        if (!ok) {
            failedLogins++;
            Serial.printf("[API] Login failed (%d/%d)\n", failedLogins, ADMIN_LOGIN_MAX_FAILS);
            if (failedLogins >= ADMIN_LOGIN_MAX_FAILS) {
                lockedUntil = millis() + ADMIN_LOGIN_LOCK_MS;
                failedLogins = 0;
                Serial.println("[API] Too many failures: login locked for 30 s.");
            }
            sendError(401, "Invalid username or password");
            return;
        }

        failedLogins = 0;
        sessionToken = SecurityUtils::randomHex(16);
        sessionLastSeen = millis();
        Serial.println("[API] Admin logged in.");

        JsonDocument doc;
        doc["success"] = true;
        doc["token"] = sessionToken;
        doc["expiresInSec"] = ADMIN_SESSION_IDLE_MS / 1000;
        doc["defaultCredentials"] = defaultCredentialsInUse();
        sendJson(200, doc);
    }

    void handleLogout() {
        if (!requireAuth()) return;
        sessionToken = "";
        sendOk();
    }

    void handleStatus() {
        if (!requireAuth()) return;
        JsonDocument doc;
        doc["state"] = runtime->state;
        doc["serviceEnabled"] = storage->getSettings().serviceEnabled;
        doc["relayOn"] = runtime->relayOn;
        doc["relayRemainingSec"] = runtime->relayRemainingSec;
        doc["paymentActive"] = runtime->paymentActive;
        if (runtime->paymentActive) {
            doc["qrId"] = runtime->activeQrId;
            doc["amount"] = runtime->activeAmount;
            doc["duration"] = runtime->activeDuration;
        }
        doc["staConnected"] = network->isConnectedToInternet();
        doc["staIP"] = network->getStaIP();
        doc["rssi"] = network->getRSSI();
        sendJson(200, doc);
    }

    void handleGetConfig() {
        if (!requireAuth()) return;
        DeviceSettings& s = storage->getSettings();

        JsonDocument doc;
        doc["name"] = s.deviceName;
        doc["fw"] = FIRMWARE_VERSION;
        doc["mac"] = WiFi.macAddress();
        doc["serviceEnabled"] = s.serviceEnabled;
        doc["backendUrl"] = s.backendUrl;
        doc["deviceKeySet"] = strlen(s.deviceKey) > 0;
        doc["adminUser"] = s.adminUser;
        doc["defaultCredentials"] = defaultCredentialsInUse();
        doc["hotspotSsid"] = s.apSSID;
        doc["wifiSsid"] = s.staSSID;
        doc["wifiConnected"] = network->isConnectedToInternet();
        doc["wifiIP"] = network->getStaIP();

        JsonArray rules = doc["rules"].to<JsonArray>();
        for (int i = 0; i < s.ruleCount; i++) {
            JsonObject r = rules.add<JsonObject>();
            r["amount"] = s.rules[i].amount;
            r["duration"] = s.rules[i].durationSec;
        }
        sendJson(200, doc);
    }

    // Partial update: only the fields present in the body are changed
    void handleSetConfig() {
        if (!requireAuth()) return;
        JsonDocument body;
        if (!readBody(body)) return;

        // Apply to a staged copy; commit only when every field validates
        DeviceSettings staged = storage->getSettings();
        DeviceSettings& s = staged;

        if (body["name"].is<String>()) {
            String name = body["name"].as<String>();
            name.trim();
            if (name.length() == 0 || name.length() > 32) {
                sendError(400, "Name must be 1-32 characters");
                return;
            }
            strncpy(s.deviceName, name.c_str(), sizeof(s.deviceName) - 1);
        }

        if (body["backendUrl"].is<String>()) {
            String url = body["backendUrl"].as<String>();
            if (!(url.startsWith("http://") || url.startsWith("https://")) || url.length() > 127) {
                sendError(400, "Backend URL must start with http:// or https://");
                return;
            }
            strncpy(s.backendUrl, url.c_str(), sizeof(s.backendUrl) - 1);
        }

        if (body["deviceKey"].is<String>()) {
            String key = body["deviceKey"].as<String>();
            if (key.length() == 0 || key.length() > 64) {
                sendError(400, "Device key must be 1-64 characters");
                return;
            }
            strncpy(s.deviceKey, key.c_str(), sizeof(s.deviceKey) - 1);
        }

        if (body["adminUser"].is<String>()) {
            String user = body["adminUser"].as<String>();
            if (user.length() == 0 || user.length() > 32) {
                sendError(400, "Admin username must be 1-32 characters");
                return;
            }
            strncpy(s.adminUser, user.c_str(), sizeof(s.adminUser) - 1);
        }

        if (body["adminPass"].is<String>()) {
            String pass = body["adminPass"].as<String>();
            if (pass.length() < 6 || pass.length() > 32) {
                sendError(400, "Admin password must be 6-32 characters");
                return;
            }
            strncpy(s.adminPass, pass.c_str(), sizeof(s.adminPass) - 1);
        }

        if (body["rules"].is<JsonArray>()) {
            JsonArray arr = body["rules"].as<JsonArray>();
            if (arr.size() > MAX_RULES) {
                sendError(400, "At most " + String(MAX_RULES) + " payment cards are allowed");
                return;
            }
            PaymentRule parsed[MAX_RULES];
            int n = 0;
            for (JsonObject r : arr) {
                float amount = r["amount"].as<float>();
                uint32_t duration = r["duration"].as<uint32_t>();
                if (amount < 1.0f || amount > 100000.0f) {
                    sendError(400, "Card amount must be between 1 and 100000");
                    return;
                }
                if (duration < 1 || duration > 3600) {
                    sendError(400, "Relay time must be between 1 and 3600 seconds");
                    return;
                }
                parsed[n++] = { amount, duration };
            }
            for (int i = 0; i < n; i++) s.rules[i] = parsed[i];
            s.ruleCount = n;
        }

        storage->getSettings() = staged;
        storage->saveSettings();
        if (cb.configChanged) cb.configChanged();
        Serial.println("[API] Configuration updated.");
        sendOk();
    }

    void handleSetService() {
        if (!requireAuth()) return;
        JsonDocument body;
        if (!readBody(body)) return;
        if (!body["enabled"].is<bool>()) {
            sendError(400, "Field 'enabled' (true/false) is required");
            return;
        }

        bool enabled = body["enabled"].as<bool>();
        DeviceSettings& s = storage->getSettings();
        s.serviceEnabled = enabled;
        storage->saveSettings();

        if (cb.serviceChanged) cb.serviceChanged(enabled);
        Serial.printf("[API] Service %s by admin.\n", enabled ? "ENABLED" : "STOPPED");

        JsonDocument doc;
        doc["success"] = true;
        doc["serviceEnabled"] = enabled;
        sendJson(200, doc);
    }

    void handleRelayTest() {
        if (!requireAuth()) return;
        JsonDocument body;
        if (!readBody(body)) return;

        if (!storage->getSettings().serviceEnabled) {
            sendError(409, "Service is stopped. Enable it first.");
            return;
        }
        if (runtime->paymentActive || runtime->relayOn) {
            sendError(409, "Relay is busy with a payment");
            return;
        }
        uint32_t seconds = body["seconds"].as<uint32_t>();
        if (seconds < 1 || seconds > 60) {
            sendError(400, "Test seconds must be between 1 and 60");
            return;
        }

        String err;
        if (cb.relayTest && cb.relayTest(seconds, err)) {
            sendOk();
        } else {
            sendError(409, err.length() ? err : "Relay test failed");
        }
    }

    void handleRelayOff() {
        if (!requireAuth()) return;
        if (cb.relayOff) cb.relayOff();
        sendOk();
    }

    void handleStartPayment() {
        if (!requireAuth()) return;
        JsonDocument body;
        if (!readBody(body)) return;

        float amount = body["amount"].as<float>();
        uint32_t duration = body["duration"].as<uint32_t>();
        String err;
        if (cb.startPayment && cb.startPayment(amount, duration, err)) {
            sendOk();
        } else {
            sendError(409, err.length() ? err : "Could not start payment");
        }
    }

    void handleCancelPayment() {
        if (!requireAuth()) return;
        if (cb.cancelPayment) cb.cancelPayment();
        sendOk();
    }

    void handleTransactions() {
        if (!requireAuth()) return;
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        int count = storage->getTransactionCount();
        const TransactionRecord* records = storage->getTransactions();
        for (int i = count - 1; i >= 0; i--) {
            JsonObject tx = arr.add<JsonObject>();
            tx["qrId"] = records[i].qrId;
            tx["paymentId"] = records[i].paymentId;
            tx["amount"] = records[i].amount;
            tx["duration"] = records[i].durationSec;
            tx["timestampSec"] = records[i].timestampSec;
            tx["success"] = records[i].success;
        }
        sendJson(200, doc);
    }

    void handleWifiScan() {
        if (!requireAuth()) return;
        if (!requireHotspot()) return;

        int n = WiFi.scanNetworks();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject net = arr.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["secured"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        WiFi.scanDelete();
        sendJson(200, doc);
    }

    void handleWifiSave() {
        if (!requireAuth()) return;
        if (!requireHotspot()) return;
        JsonDocument body;
        if (!readBody(body)) return;

        String ssid = body["ssid"].as<String>();
        String pass = body["pass"].as<String>();
        if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
            sendError(400, "SSID must be 1-32 characters and password at most 64");
            return;
        }

        DeviceSettings& s = storage->getSettings();
        strncpy(s.staSSID, ssid.c_str(), sizeof(s.staSSID) - 1);
        strncpy(s.staPass, pass.c_str(), sizeof(s.staPass) - 1);
        storage->saveSettings();
        network->setCredentials(ssid, pass);
        Serial.printf("[API] Wi-Fi credentials saved for '%s'.\n", ssid.c_str());
        sendOk();
    }

    void handleRestart() {
        if (!requireAuth()) return;
        sendOk();
        delay(500); // let the response leave the device before rebooting
        ESP.restart();
    }

    void handleNotFound() {
        sendError(404, "Unknown endpoint");
    }

public:
    AdminApi() {}

    void begin(StorageManager* stor, RelayController* rel, NetworkManager* net,
               const RuntimeStatus* status, AdminCallbacks callbacks) {
        storage = stor;
        relay = rel;
        network = net;
        runtime = status;
        cb = callbacks;

        const char* headerKeys[] = { "Authorization" };
        server.collectHeaders(headerKeys, 1);

        server.on("/api/v1/info", HTTP_GET, [this]() { handleInfo(); });
        server.on("/api/v1/auth/challenge", HTTP_GET, [this]() { handleChallenge(); });
        server.on("/api/v1/auth/login", HTTP_POST, [this]() { handleLogin(); });
        server.on("/api/v1/auth/logout", HTTP_POST, [this]() { handleLogout(); });

        server.on("/api/v1/status", HTTP_GET, [this]() { handleStatus(); });
        server.on("/api/v1/config", HTTP_GET, [this]() { handleGetConfig(); });
        server.on("/api/v1/config", HTTP_POST, [this]() { handleSetConfig(); });
        server.on("/api/v1/service", HTTP_POST, [this]() { handleSetService(); });
        server.on("/api/v1/relay/test", HTTP_POST, [this]() { handleRelayTest(); });
        server.on("/api/v1/relay/off", HTTP_POST, [this]() { handleRelayOff(); });
        server.on("/api/v1/payment/start", HTTP_POST, [this]() { handleStartPayment(); });
        server.on("/api/v1/payment/cancel", HTTP_POST, [this]() { handleCancelPayment(); });
        server.on("/api/v1/transactions", HTTP_GET, [this]() { handleTransactions(); });
        server.on("/api/v1/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
        server.on("/api/v1/wifi", HTTP_POST, [this]() { handleWifiSave(); });
        server.on("/api/v1/restart", HTTP_POST, [this]() { handleRestart(); });

        server.onNotFound([this]() { handleNotFound(); });
        server.begin();
        Serial.printf("[API] Admin API listening on port %d (LAN + hotspot)\n", ADMIN_API_PORT);
    }

    void update() {
        server.handleClient();
    }
};

#endif // ADMIN_API_H
