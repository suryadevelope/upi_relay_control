#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "StorageManager.h"
#include "RelayController.h"
#include "NetworkManager.h"

// Forward declaration of payment trigger callback
typedef std::function<bool(float, uint32_t, String&)> PaymentTriggerCallback;
typedef std::function<void()> PaymentCancelCallback;

class WebServerManager {
private:
    WebServer server{80};
    StorageManager* storage = nullptr;
    RelayController* relay = nullptr;
    NetworkManager* network = nullptr;

    PaymentTriggerCallback onPaymentTrigger = nullptr;
    PaymentCancelCallback onPaymentCancel = nullptr;

    String currentSessionToken = "";
    unsigned long sessionExpiry = 0;

    String activeQrPayload = "";
    String activeQrId = "";
    float activeAmount = 0.0f;
    uint32_t activeDuration = 0;

    bool isAuthenticated() {
        if (!server.hasHeader("Cookie")) return false;
        String cookie = server.header("Cookie");
        if (currentSessionToken.length() > 0 && 
            cookie.indexOf("SESSION=" + currentSessionToken) != -1 &&
            millis() < sessionExpiry) {
            return true;
        }
        return false;
    }

    String generateSessionToken() {
        return String(random(10000000, 99999999), HEX) + String(millis(), HEX);
    }

public:
    WebServerManager() {}

    void begin(StorageManager* stor, RelayController* rel, NetworkManager* net) {
        storage = stor;
        relay = rel;
        network = net;

        const char* headerKeys[] = { "Cookie" };
        server.collectHeaders(headerKeys, 1);

        // Routing
        server.on("/", HTTP_GET, [this]() { handleRoot(); });
        server.on("/login", HTTP_POST, [this]() { handleLogin(); });
        server.on("/logout", HTTP_POST, [this]() { handleLogout(); });

        server.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
        server.on("/api/pay/trigger", HTTP_POST, [this]() { handlePayTrigger(); });
        server.on("/api/pay/cancel", HTTP_POST, [this]() { handlePayCancel(); });

        server.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
        server.on("/api/wifi/save", HTTP_POST, [this]() { handleWifiSave(); });

        server.on("/api/rules/save", HTTP_POST, [this]() { handleRulesSave(); });
        server.on("/api/settings/save", HTTP_POST, [this]() { handleSettingsSave(); });
        server.on("/api/relay/pulse", HTTP_POST, [this]() { handleRelayPulse(); });

        server.on("/api/transactions", HTTP_GET, [this]() { handleGetTransactions(); });
        server.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });

        server.begin();
        Serial.println("[WEBSERVER] HTTP Admin server started on port 80.");
    }

    void update() {
        server.handleClient();
    }

    void setPaymentCallbacks(PaymentTriggerCallback onTrigger, PaymentCancelCallback onCancel) {
        onPaymentTrigger = onTrigger;
        onPaymentCancel = onCancel;
    }

    void setActivePaymentSession(const String& qrId, const String& qrPayload, float amount, uint32_t duration) {
        activeQrId = qrId;
        activeQrPayload = qrPayload;
        activeAmount = amount;
        activeDuration = duration;
    }

    void clearActivePaymentSession() {
        activeQrId = "";
        activeQrPayload = "";
        activeAmount = 0.0f;
        activeDuration = 0;
    }

private:
    void handleRoot() {
        // If not authenticated, serve the login page
        if (!isAuthenticated()) {
            server.send(200, "text/html", getLoginPageHtml());
            return;
        }
        // Serve Main Admin Portal
        server.send(200, "text/html", getMainDashboardHtml());
    }

    void handleLogin() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        String user = doc["username"].as<String>();
        String pass = doc["password"].as<String>();

        DeviceSettings& s = storage->getSettings();
        if (user == s.adminUser && pass == s.adminPass) {
            currentSessionToken = generateSessionToken();
            sessionExpiry = millis() + (3600UL * 1000UL * 2); // 2 hours
            server.sendHeader("Set-Cookie", "SESSION=" + currentSessionToken + "; Path=/; HttpOnly");
            server.send(200, "application/json", "{\"success\":true}");
        } else {
            server.send(401, "application/json", "{\"error\":\"Invalid credentials\"}");
        }
    }

    void handleLogout() {
        currentSessionToken = "";
        sessionExpiry = 0;
        server.sendHeader("Set-Cookie", "SESSION=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
        server.send(200, "application/json", "{\"success\":true}");
    }

    void handleGetStatus() {
        JsonDocument doc;
        GatewayState st = relay->getState();
        doc["state"] = stateToString(st);
        doc["relayOn"] = relay->isRelayOn();
        doc["remainingSec"] = relay->getRemainingSeconds();
        doc["totalDurationSec"] = relay->getTotalDurationSeconds();

        doc["activeQrId"] = activeQrId;
        doc["activeQrPayload"] = activeQrPayload;
        doc["activeAmount"] = activeAmount;
        doc["activeDuration"] = activeDuration;

        doc["staConnected"] = network->isConnectedToInternet();
        doc["staIP"] = network->getStaIP();
        doc["apIP"] = network->getApIP();
        doc["rssi"] = network->getRSSI();
        doc["uptimeSec"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();

        DeviceSettings& s = storage->getSettings();
        doc["debugMode"] = s.debugMode;
        doc["relayPin"] = s.relayPin;
        doc["backendUrl"] = s.backendUrl;

        JsonArray rulesArr = doc["rules"].to<JsonArray>();
        for (int i = 0; i < s.ruleCount; i++) {
            JsonObject r = rulesArr.add<JsonObject>();
            r["amount"] = s.rules[i].amount;
            r["duration"] = s.rules[i].durationSec;
        }

        String res;
        serializeJson(doc, res);
        server.send(200, "application/json", res);
    }

    void handlePayTrigger() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }

        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        float amount = doc["amount"].as<float>();
        uint32_t duration = doc["duration"].as<uint32_t>();
        if (duration == 0) {
            duration = storage->getDurationForAmount(amount);
        }

        if (amount <= 0.0f) {
            server.send(400, "application/json", "{\"error\":\"Invalid amount\"}");
            return;
        }

        if (relay->getState() == STATE_RELAY_ON) {
            server.send(400, "application/json", "{\"error\":\"Relay currently active\"}");
            return;
        }

        String err;
        if (onPaymentTrigger && onPaymentTrigger(amount, duration, err)) {
            server.send(200, "application/json", "{\"success\":true}");
        } else {
            // Serialize properly: backend error text may contain quotes
            JsonDocument res;
            res["error"] = err;
            String body;
            serializeJson(res, body);
            server.send(500, "application/json", body);
        }
    }

    void handlePayCancel() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (onPaymentCancel) onPaymentCancel();
        clearActivePaymentSession();
        relay->setState(STATE_READY);
        server.send(200, "application/json", "{\"success\":true}");
    }

    void handleWifiScan() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }

        int n = WiFi.scanNetworks();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < n; ++i) {
            JsonObject net = arr.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
        }

        String res;
        serializeJson(doc, res);
        server.send(200, "application/json", res);
    }

    void handleWifiSave() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }

        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        String ssid = doc["ssid"].as<String>();
        String pass = doc["pass"].as<String>();

        DeviceSettings& s = storage->getSettings();
        strncpy(s.staSSID, ssid.c_str(), sizeof(s.staSSID) - 1);
        strncpy(s.staPass, pass.c_str(), sizeof(s.staPass) - 1);
        storage->saveSettings();

        network->setCredentials(ssid, pass);
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Connecting to Wi-Fi...\"}");
    }

    void handleRulesSave() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }

        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));
        JsonArray arr = doc["rules"].as<JsonArray>();

        DeviceSettings& s = storage->getSettings();
        s.ruleCount = arr.size() > MAX_RULES ? MAX_RULES : arr.size();

        for (int i = 0; i < s.ruleCount; i++) {
            s.rules[i].amount = arr[i]["amount"].as<float>();
            s.rules[i].durationSec = arr[i]["duration"].as<uint32_t>();
        }

        storage->saveSettings();
        server.send(200, "application/json", "{\"success\":true}");
    }

    void handleSettingsSave() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }

        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        DeviceSettings& s = storage->getSettings();

        if (doc["backendUrl"].is<String>()) {
            strncpy(s.backendUrl, doc["backendUrl"].as<String>().c_str(), sizeof(s.backendUrl) - 1);
        }
        if (doc["deviceKey"].is<String>()) {
            strncpy(s.deviceKey, doc["deviceKey"].as<String>().c_str(), sizeof(s.deviceKey) - 1);
        }
        if (doc["adminPass"].is<String>() && doc["adminPass"].as<String>().length() >= 4) {
            strncpy(s.adminPass, doc["adminPass"].as<String>().c_str(), sizeof(s.adminPass) - 1);
        }
        if (doc["relayPin"].is<int>()) {
            s.relayPin = doc["relayPin"].as<int>();
        }
        if (doc["relayActiveHigh"].is<bool>()) {
            s.relayActiveHigh = doc["relayActiveHigh"].as<bool>();
        }
        if (doc["debugMode"].is<bool>()) {
            s.debugMode = doc["debugMode"].as<bool>();
        }
        if (doc["displayType"].is<int>()) {
            s.displayType = doc["displayType"].as<int>();
        }
        if (doc["tftRotation"].is<int>()) {
            s.tftRotation = doc["tftRotation"].as<int>();
        }

        storage->saveSettings();
        server.send(200, "application/json", "{\"success\":true}");
    }

    void handleRelayPulse() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }

        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));
        uint32_t duration = doc["duration"].as<uint32_t>();
        if (duration == 0) duration = 3;

        relay->activate(duration);
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Relay pulsed\"}");
    }

    void handleGetTransactions() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        int count = storage->getTransactionCount();
        const TransactionRecord* records = storage->getTransactions();

        for (int i = count - 1; i >= 0; i--) { // Newest first
            JsonObject tx = arr.add<JsonObject>();
            tx["qrId"] = records[i].qrId;
            tx["paymentId"] = records[i].paymentId;
            tx["amount"] = records[i].amount;
            tx["duration"] = records[i].durationSec;
            tx["timestampSec"] = records[i].timestampSec;
            tx["success"] = records[i].success;
        }

        String res;
        serializeJson(doc, res);
        server.send(200, "application/json", res);
    }

    void handleReboot() {
        if (!isAuthenticated()) {
            server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting ESP32...\"}");
        delay(1000);
        ESP.restart();
    }

    // HTML Web Pages (Modern Dark Theme UI)
    String getLoginPageHtml() {
        return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Login - Smart Payment Gateway</title>
<style>
:root { --bg: #0f172a; --card: #1e293b; --text: #f8fafc; --primary: #3b82f6; --accent: #10b981; }
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
body { background: var(--bg); color: var(--text); display: flex; align-items: center; justify-content: center; min-height: 100vh; padding: 20px; }
.card { background: var(--card); border-radius: 16px; padding: 32px; width: 100%; max-width: 400px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); border: 1px solid #334155; }
h1 { font-size: 24px; font-weight: 700; margin-bottom: 8px; color: #60a5fa; text-align: center; }
p { font-size: 14px; color: #94a3b8; text-align: center; margin-bottom: 24px; }
.input-group { margin-bottom: 16px; }
label { display: block; font-size: 13px; font-weight: 500; margin-bottom: 6px; color: #cbd5e1; }
input { width: 100%; padding: 12px; border-radius: 8px; border: 1px solid #475569; background: #0f172a; color: #fff; font-size: 15px; }
input:focus { outline: none; border-color: var(--primary); }
button { width: 100%; padding: 12px; border-radius: 8px; border: none; background: var(--primary); color: #fff; font-size: 16px; font-weight: 600; cursor: pointer; transition: 0.2s; margin-top: 8px; }
button:hover { background: #2563eb; }
.alert { margin-top: 16px; padding: 10px; border-radius: 8px; font-size: 13px; display: none; text-align: center; background: #ef444422; border: 1px solid #ef4444; color: #fca5a5; }
</style>
</head>
<body>
<div class="card">
  <h1>Smart Gateway</h1>
  <p>ESP32 Payment & Relay Control Portal</p>
  <form id="loginForm">
    <div class="input-group">
      <label>Username</label>
      <input type="text" id="username" value="admin" required>
    </div>
    <div class="input-group">
      <label>Password</label>
      <input type="password" id="password" required placeholder="Enter admin password">
    </div>
    <button type="submit">Sign In</button>
    <div class="alert" id="alertMsg"></div>
  </form>
</div>
<script>
document.getElementById('loginForm').onsubmit = async (e) => {
  e.preventDefault();
  const alert = document.getElementById('alertMsg');
  alert.style.display = 'none';
  try {
    const res = await fetch('/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: document.getElementById('username').value,
        password: document.getElementById('password').value
      })
    });
    const data = await res.json();
    if (res.ok && data.success) {
      window.location.reload();
    } else {
      alert.textContent = data.error || 'Invalid credentials';
      alert.style.display = 'block';
    }
  } catch (err) {
    alert.textContent = 'Connection error';
    alert.style.display = 'block';
  }
};
</script>
</body>
</html>
)rawliteral";
    }

    String getMainDashboardHtml() {
        return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Admin Portal - Smart Payment Gateway</title>
<!-- QRious library for rendering live QR code directly on web browser -->
<script src="https://cdnjs.cloudflare.com/ajax/libs/qrious/4.0.2/qrious.min.js"></script>
<style>
:root {
  --bg: #090d16;
  --surface: #131b2e;
  --card: #1c2640;
  --primary: #3b82f6;
  --success: #10b981;
  --warning: #f59e0b;
  --danger: #ef4444;
  --text: #f8fafc;
  --muted: #94a3b8;
  --border: #2c3a59;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
body { background: var(--bg); color: var(--text); padding-bottom: 40px; }
header { background: var(--surface); border-bottom: 1px solid var(--border); padding: 16px 24px; display: flex; align-items: center; justify-content: space-between; position: sticky; top: 0; z-index: 50; }
.logo { font-size: 20px; font-weight: 700; color: #60a5fa; display: flex; align-items: center; gap: 10px; }
.state-badge { padding: 4px 12px; border-radius: 9999px; font-size: 13px; font-weight: 600; text-transform: uppercase; }
.state-READY { background: #10b98133; color: #34d399; border: 1px solid #10b981; }
.state-QR_ACTIVE { background: #3b82f633; color: #60a5fa; border: 1px solid #3b82f6; }
.state-VERIFYING { background: #f59e0b33; color: #fbbf24; border: 1px solid #f59e0b; }
.state-PAID, .state-RELAY_ON { background: #8b5cf633; color: #a78bfa; border: 1px solid #8b5cf6; }
.state-COMPLETE { background: #10b98133; color: #34d399; border: 1px solid #10b981; }
.state-ERROR { background: #ef444433; color: #f87171; border: 1px solid #ef4444; }

nav { display: flex; gap: 8px; padding: 12px 24px; background: var(--surface); border-bottom: 1px solid var(--border); overflow-x: auto; }
nav button { padding: 8px 16px; border-radius: 8px; border: none; background: transparent; color: var(--muted); font-size: 14px; font-weight: 600; cursor: pointer; transition: 0.2s; white-space: nowrap; }
nav button.active { background: var(--primary); color: #fff; }

.container { max-width: 1000px; margin: 24px auto; padding: 0 16px; }
.tab-content { display: none; }
.tab-content.active { display: block; }

.card { background: var(--card); border-radius: 12px; padding: 20px; margin-bottom: 20px; border: 1px solid var(--border); }
.card h2 { font-size: 18px; margin-bottom: 16px; color: #93c5fd; display: flex; align-items: center; justify-content: space-between; }
.grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
.grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 12px; }

.btn { padding: 10px 16px; border-radius: 8px; border: none; font-weight: 600; font-size: 14px; cursor: pointer; transition: 0.2s; }
.btn-primary { background: var(--primary); color: #fff; }
.btn-primary:hover { background: #2563eb; }
.btn-success { background: var(--success); color: #fff; }
.btn-danger { background: var(--danger); color: #fff; }
.btn-warning { background: var(--warning); color: #111; }
.btn-secondary { background: #334155; color: #fff; }

.qr-box { display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 20px; background: #0f172a; border-radius: 12px; border: 1px dashed var(--border); text-align: center; }
#qrCanvas { margin: 16px 0; border: 8px solid #fff; border-radius: 8px; }

.form-group { margin-bottom: 14px; }
label { display: block; font-size: 13px; color: var(--muted); margin-bottom: 6px; }
input, select { width: 100%; padding: 10px; border-radius: 8px; border: 1px solid var(--border); background: #0f172a; color: #fff; font-size: 14px; }
input:focus, select:focus { outline: none; border-color: var(--primary); }

.price-card { background: #0f172a; border: 1px solid var(--border); border-radius: 8px; padding: 14px; text-align: center; cursor: pointer; transition: 0.2s; }
.price-card:hover { border-color: var(--primary); transform: translateY(-2px); }
.price-amount { font-size: 22px; font-weight: 700; color: #34d399; }
.price-duration { font-size: 13px; color: var(--muted); margin-top: 4px; }

table { width: 100%; border-collapse: collapse; margin-top: 12px; }
th, td { padding: 10px 12px; text-align: left; font-size: 13px; border-bottom: 1px solid var(--border); }
th { color: var(--muted); font-weight: 600; }

.switch { position: relative; display: inline-block; width: 44px; height: 24px; }
.switch input { opacity: 0; width: 0; height: 0; }
.slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #334155; transition: .4s; border-radius: 24px; }
.slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .4s; border-radius: 50%; }
input:checked + .slider { background-color: var(--primary); }
input:checked + .slider:before { transform: translateX(20px); }

.timer-box { font-size: 48px; font-weight: 800; color: #34d399; text-align: center; margin: 16px 0; }
</style>
</head>
<body>
<header>
  <div class="logo">
    ⚡ Smart Gateway Admin
  </div>
  <div style="display: flex; align-items: center; gap: 12px;">
    <span class="state-badge" id="stateBadge">LOADING</span>
    <button class="btn btn-secondary" style="padding: 6px 12px;" onclick="logout()">Logout</button>
  </div>
</header>

<nav>
  <button class="active" onclick="openTab('dashboard')">📊 Dashboard & Pay</button>
  <button onclick="openTab('wifi')">📶 Wi-Fi Setup</button>
  <button onclick="openTab('rules')">💰 Pricing Rules</button>
  <button onclick="openTab('hardware')">🔌 Relay & Hardware</button>
  <button onclick="openTab('cloud')">☁️ Cloud & Security</button>
  <button onclick="openTab('logs')">📜 History & Logs</button>
</nav>

<div class="container">
  <!-- TAB: DASHBOARD & LIVE PAYMENT -->
  <div id="tab-dashboard" class="tab-content active">
    <div class="grid-2">
      <div class="card">
        <h2>Initiate Payment</h2>
        <p style="font-size: 13px; color: var(--muted); margin-bottom: 12px;">Select configured amount or type custom value:</p>
        <div class="grid-4" id="quickButtons" style="margin-bottom: 16px;"></div>
        
        <div class="form-group">
          <label>Custom Amount (₹)</label>
          <input type="number" id="customAmount" placeholder="e.g. 20.00" step="1">
        </div>
        <div class="form-group">
          <label>Duration (Seconds)</label>
          <input type="number" id="customDuration" placeholder="e.g. 30" step="1">
        </div>
        <div style="display: flex; gap: 10px;">
          <button class="btn btn-primary" style="flex: 1;" onclick="triggerPayment()">Generate Payment QR</button>
          <button class="btn btn-danger" onclick="cancelPayment()">Cancel / Reset</button>
        </div>
      </div>

      <div class="card">
        <h2>Live QR & Status</h2>
        <div class="qr-box" id="qrContainer">
          <p id="qrPrompt" style="color: var(--muted);">No active payment session. Select an amount on the left.</p>
          <canvas id="qrCanvas" style="display: none;"></canvas>
          <div id="paymentDetails" style="display: none; margin-top: 10px;">
            <div style="font-size: 20px; font-weight: 700; color: #34d399;" id="displayAmount">₹0.00</div>
            <div style="font-size: 13px; color: var(--muted);" id="displayDuration">Duration: 0s</div>
            <div style="font-size: 12px; color: #60a5fa; margin-top: 4px;" id="displayQrId"></div>
          </div>
          <div id="activeRelayBox" style="display: none; width: 100%;">
            <div style="color: #60a5fa; font-weight: 600; text-transform: uppercase;">Relay Activated (ON)</div>
            <div class="timer-box" id="relayTimer">0s</div>
            <p style="font-size: 13px; color: var(--muted);">Powering load... Automatic shutoff when timer hits 0.</p>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB: WI-FI SETUP -->
  <div id="tab-wifi" class="tab-content">
    <div class="card">
      <h2>Internet Wi-Fi Connection</h2>
      <div style="margin-bottom: 16px; padding: 12px; background: #0f172a; border-radius: 8px;">
        <div>Station Status: <strong id="staStatusText" style="color: #34d399;">Checking...</strong></div>
        <div style="font-size: 13px; color: var(--muted); margin-top: 4px;">Local IP: <span id="staIpText">-</span> | Signal: <span id="staRssiText">-</span> dBm</div>
        <div style="font-size: 13px; color: var(--muted);">Hotspot Setup IP: <strong>192.168.4.1</strong></div>
      </div>

      <div style="display: flex; gap: 10px; margin-bottom: 16px;">
        <button class="btn btn-secondary" onclick="scanWifi()">🔍 Scan Available Networks</button>
      </div>

      <div class="form-group">
        <label>Select Network (SSID)</label>
        <select id="wifiSsidSelect" onchange="selectSsid(this.value)">
          <option value="">-- Click Scan Networks above --</option>
        </select>
      </div>
      <div class="form-group">
        <label>Or Enter SSID Manually</label>
        <input type="text" id="wifiSsidManual" placeholder="Router Wi-Fi SSID">
      </div>
      <div class="form-group">
        <label>Wi-Fi Password</label>
        <input type="password" id="wifiPassword" placeholder="Router Wi-Fi Password">
      </div>
      <button class="btn btn-primary" onclick="saveWifi()">Save & Connect</button>
    </div>
  </div>

  <!-- TAB: PRICING RULES -->
  <div id="tab-rules" class="tab-content">
    <div class="card">
      <h2>Amount-to-Duration Rules</h2>
      <p style="font-size: 13px; color: var(--muted); margin-bottom: 12px;">Map payment amounts in ₹ to relay activation durations in seconds:</p>
      
      <table id="rulesTable">
        <thead>
          <tr>
            <th>Amount (₹)</th>
            <th>Duration (Sec)</th>
            <th>Action</th>
          </tr>
        </thead>
        <tbody></tbody>
      </table>

      <div style="margin-top: 16px; display: flex; gap: 10px;">
        <button class="btn btn-secondary" onclick="addRuleRow(20, 30)">+ Add Rule</button>
        <button class="btn btn-success" onclick="saveRules()">Save Rules</button>
      </div>
    </div>
  </div>

  <!-- TAB: RELAY & HARDWARE -->
  <div id="tab-hardware" class="tab-content">
    <div class="card">
      <h2>Relay Hardware & Manual Test</h2>
      <div class="grid-2">
        <div>
          <div class="form-group">
            <label>Relay Control GPIO Pin</label>
            <input type="number" id="relayPinInput" value="26">
          </div>
          <div class="form-group">
            <label>Relay Logic Level</label>
            <select id="relayLogicSelect">
              <option value="1">Active HIGH (Pin HIGH = Relay ON)</option>
              <option value="0">Active LOW (Pin LOW = Relay ON)</option>
            </select>
          </div>
          <button class="btn btn-primary" onclick="saveHardware()">Save Hardware Settings</button>
        </div>
        <div style="padding: 16px; background: #0f172a; border-radius: 8px; border: 1px solid var(--border);">
          <h3 style="font-size: 15px; margin-bottom: 8px; color: #fbbf24;">Manual Relay Pulse Test</h3>
          <p style="font-size: 13px; color: var(--muted); margin-bottom: 12px;">Test relay hardware without payment verification:</p>
          <div class="form-group">
            <label>Pulse Duration (Seconds)</label>
            <input type="number" id="pulseDuration" value="3" min="1" max="60">
          </div>
          <button class="btn btn-warning" onclick="triggerPulse()">⚡ Test Relay Pulse</button>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB: CLOUD & SECURITY -->
  <div id="tab-cloud" class="tab-content">
    <div class="card">
      <h2>Cloud Backend & Security Config</h2>
      <div class="form-group">
        <label>Cloud Backend Base URL</label>
        <input type="text" id="backendUrlInput" placeholder="http://10.13.137.189:3000">
        <small style="color: var(--muted); font-size: 12px;">Enter your Cloudflare Worker URL or Node.js server address.</small>
      </div>
      <div class="form-group">
        <label>Device Secret Key (X-Device-Key)</label>
        <input type="password" id="deviceKeyInput" placeholder="Shared authentication secret">
      </div>
      <div class="form-group">
        <label>Change Administrator Web Password</label>
        <input type="password" id="newAdminPass" placeholder="Leave empty to keep current password">
      </div>
      
      <!-- USER SPECIAL REQUIREMENT: SERIAL DEBUG MODE -->
      <div style="margin: 20px 0; padding: 16px; background: #0f172a; border-radius: 8px; border: 1px solid var(--border); display: flex; align-items: center; justify-content: space-between;">
        <div>
          <strong style="color: #60a5fa;">Debug Mode (Serial Monitor QR Code)</strong>
          <p style="font-size: 12px; color: var(--muted); margin-top: 4px;">When enabled, prints full scannable ASCII/UTF-8 QR codes and detailed payment diagnostics directly into the PlatformIO Serial Monitor.</p>
        </div>
        <label class="switch">
          <input type="checkbox" id="debugModeToggle">
          <span class="slider"></span>
        </label>
      </div>

      <button class="btn btn-primary" onclick="saveCloudSettings()">Save Security Settings</button>
    </div>
  </div>

  <!-- TAB: HISTORY & LOGS -->
  <div id="tab-logs" class="tab-content">
    <div class="card">
      <h2>System Diagnostics & Transaction History</h2>
      <div class="grid-2" style="margin-bottom: 16px;">
        <div style="padding: 12px; background: #0f172a; border-radius: 8px;">
          <div>Uptime: <span id="uptimeText">-</span></div>
          <div>Free Memory: <span id="heapText">-</span> KB</div>
        </div>
        <div style="display: flex; gap: 10px; align-items: center;">
          <button class="btn btn-secondary" onclick="loadTransactions()">Refresh History</button>
          <button class="btn btn-danger" onclick="rebootEsp()">Reboot ESP32</button>
        </div>
      </div>

      <table id="txTable">
        <thead>
          <tr>
            <th>Payment ID</th>
            <th>Amount</th>
            <th>Duration</th>
            <th>Status</th>
          </tr>
        </thead>
        <tbody id="txBody">
          <tr><td colspan="4" style="text-align: center; color: var(--muted);">Loading transactions...</td></tr>
        </tbody>
      </table>
    </div>
  </div>
</div>

<script>
let qrGenerator = null;
let currentRules = [];

function openTab(tabName) {
  document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
  event.target.classList.add('active');
  document.getElementById('tab-' + tabName).classList.add('active');
  if (tabName === 'logs') loadTransactions();
}

async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    if (!res.ok) return;
    const data = await res.json();

    // Update state badge
    const badge = document.getElementById('stateBadge');
    badge.textContent = data.state;
    badge.className = 'state-badge state-' + data.state;

    // Station status
    document.getElementById('staStatusText').textContent = data.staConnected ? 'Connected' : 'Disconnected';
    document.getElementById('staStatusText').style.color = data.staConnected ? '#34d399' : '#f87171';
    document.getElementById('staIpText').textContent = data.staIP;
    document.getElementById('staRssiText').textContent = data.rssi;

    // System stats
    document.getElementById('uptimeText').textContent = Math.floor(data.uptimeSec / 60) + ' mins';
    document.getElementById('heapText').textContent = Math.round(data.freeHeap / 1024);

    // Debug mode
    document.getElementById('debugModeToggle').checked = data.debugMode;
    document.getElementById('backendUrlInput').value = data.backendUrl || '';

    // Quick buttons
    if (data.rules && data.rules.length > 0) {
      currentRules = data.rules;
      renderQuickButtons(data.rules);
    }

    // Live QR rendering & relay state
    const qrCanvas = document.getElementById('qrCanvas');
    const qrPrompt = document.getElementById('qrPrompt');
    const paymentDetails = document.getElementById('paymentDetails');
    const activeRelayBox = document.getElementById('activeRelayBox');

    if (data.relayOn) {
      qrCanvas.style.display = 'none';
      qrPrompt.style.display = 'none';
      paymentDetails.style.display = 'none';
      activeRelayBox.style.display = 'block';
      document.getElementById('relayTimer').textContent = data.remainingSec + 's';
    } else if (data.activeQrPayload && data.activeQrPayload.length > 0) {
      activeRelayBox.style.display = 'none';
      qrPrompt.style.display = 'none';
      qrCanvas.style.display = 'block';
      paymentDetails.style.display = 'block';

      document.getElementById('displayAmount').textContent = '₹' + data.activeAmount.toFixed(2);
      document.getElementById('displayDuration').textContent = 'Duration: ' + data.activeDuration + 's';
      document.getElementById('displayQrId').textContent = 'ID: ' + data.activeQrId;

      if (!qrGenerator) {
        qrGenerator = new QRious({ element: qrCanvas, size: 200, value: data.activeQrPayload });
      } else {
        qrGenerator.set({ value: data.activeQrPayload });
      }
    } else {
      activeRelayBox.style.display = 'none';
      qrCanvas.style.display = 'none';
      paymentDetails.style.display = 'none';
      qrPrompt.style.display = 'block';
    }
  } catch (e) {
    console.error(e);
  }
}

function renderQuickButtons(rules) {
  const container = document.getElementById('quickButtons');
  container.innerHTML = '';
  rules.forEach(r => {
    const card = document.createElement('div');
    card.className = 'price-card';
    card.innerHTML = `<div class="price-amount">₹${r.amount}</div><div class="price-duration">${r.duration} Sec</div>`;
    card.onclick = () => {
      document.getElementById('customAmount').value = r.amount;
      document.getElementById('customDuration').value = r.duration;
      triggerPayment(r.amount, r.duration);
    };
    container.appendChild(card);
  });
}

async function triggerPayment(amt, dur) {
  const amount = amt || parseFloat(document.getElementById('customAmount').value);
  const duration = dur || parseInt(document.getElementById('customDuration').value) || 0;
  if (!amount || amount <= 0) {
    alert('Please enter a valid payment amount');
    return;
  }
  try {
    const res = await fetch('/api/pay/trigger', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ amount, duration })
    });
    const d = await res.json();
    if (!res.ok) alert(d.error || 'Failed to trigger payment');
    fetchStatus();
  } catch (e) {
    alert('Request failed');
  }
}

async function cancelPayment() {
  await fetch('/api/pay/cancel', { method: 'POST' });
  fetchStatus();
}

async function scanWifi() {
  const sel = document.getElementById('wifiSsidSelect');
  sel.innerHTML = '<option>Scanning...</option>';
  try {
    const res = await fetch('/api/wifi/scan');
    const list = await res.json();
    sel.innerHTML = '<option value="">-- Choose Wi-Fi --</option>';
    list.forEach(n => {
      const opt = document.createElement('option');
      opt.value = n.ssid;
      opt.textContent = `${n.ssid} (${n.rssi} dBm, ${n.encryption})`;
      sel.appendChild(opt);
    });
  } catch (e) {
    sel.innerHTML = '<option>Scan failed</option>';
  }
}

function selectSsid(val) {
  if (val) document.getElementById('wifiSsidManual').value = val;
}

async function saveWifi() {
  const ssid = document.getElementById('wifiSsidManual').value;
  const pass = document.getElementById('wifiPassword').value;
  if (!ssid) { alert('Enter SSID'); return; }
  const res = await fetch('/api/wifi/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid, pass })
  });
  const d = await res.json();
  alert(d.message || 'Saved');
}

function renderRulesTable() {
  const tbody = document.querySelector('#rulesTable tbody');
  tbody.innerHTML = '';
  currentRules.forEach((r, idx) => {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td><input type="number" value="${r.amount}" style="width: 100px;" onchange="currentRules[${idx}].amount=parseFloat(this.value)"></td>
      <td><input type="number" value="${r.duration}" style="width: 100px;" onchange="currentRules[${idx}].duration=parseInt(this.value)"></td>
      <td><button class="btn btn-danger" style="padding: 4px 8px;" onclick="removeRule(${idx})">Delete</button></td>
    `;
    tbody.appendChild(tr);
  });
}

function addRuleRow(amt, dur) {
  currentRules.push({ amount: amt, duration: dur });
  renderRulesTable();
}

function removeRule(idx) {
  currentRules.splice(idx, 1);
  renderRulesTable();
}

async function saveRules() {
  await fetch('/api/rules/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ rules: currentRules })
  });
  alert('Rules saved successfully');
  fetchStatus();
}

async function triggerPulse() {
  const dur = parseInt(document.getElementById('pulseDuration').value) || 3;
  await fetch('/api/relay/pulse', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ duration: dur })
  });
  fetchStatus();
}

async function saveHardware() {
  const relayPin = parseInt(document.getElementById('relayPinInput').value);
  const relayActiveHigh = document.getElementById('relayLogicSelect').value === "1";
  await fetch('/api/settings/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ relayPin, relayActiveHigh })
  });
  alert('Hardware settings saved');
}

async function saveCloudSettings() {
  const backendUrl = document.getElementById('backendUrlInput').value;
  const deviceKey = document.getElementById('deviceKeyInput').value;
  const adminPass = document.getElementById('newAdminPass').value;
  const debugMode = document.getElementById('debugModeToggle').checked;

  const payload = { backendUrl, debugMode };
  if (deviceKey) payload.deviceKey = deviceKey;
  if (adminPass) payload.adminPass = adminPass;

  await fetch('/api/settings/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  alert('Security & Debug settings updated');
  fetchStatus();
}

async function loadTransactions() {
  try {
    const res = await fetch('/api/transactions');
    const list = await res.json();
    const tbody = document.getElementById('txBody');
    tbody.innerHTML = '';
    if (list.length === 0) {
      tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--muted);">No transactions recorded yet</td></tr>';
      return;
    }
    list.forEach(tx => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td style="font-family: monospace;">${tx.paymentId || 'N/A'}</td>
        <td style="color: #34d399; font-weight: 600;">₹${tx.amount.toFixed(2)}</td>
        <td>${tx.duration}s</td>
        <td><span class="state-badge ${tx.success ? 'state-READY' : 'state-ERROR'}">${tx.success ? 'PAID' : 'FAILED'}</span></td>
      `;
      tbody.appendChild(tr);
    });
  } catch (e) {
    console.error(e);
  }
}

async function rebootEsp() {
  if (confirm('Are you sure you want to reboot the ESP32?')) {
    await fetch('/api/reboot', { method: 'POST' });
    alert('Rebooting...');
  }
}

async function logout() {
  await fetch('/logout', { method: 'POST' });
  window.location.reload();
}

// Initial poll and recurring update loop
setInterval(fetchStatus, 1500);
fetchStatus();
</script>
</body>
</html>
)rawliteral";
    }
};

#endif // WEB_SERVER_MANAGER_H
