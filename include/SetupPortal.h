#ifndef SETUP_PORTAL_H
#define SETUP_PORTAL_H

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "StorageManager.h"
#include "NetworkManager.h"
#include "SecurityUtils.h"

// Wi-Fi setup page reachable ONLY from the device's own hotspot (SoftAP).
// Every request from the LAN is refused, so no webpage is ever served over the
// station network. Requires the device admin login (HTTP Basic over WPA2 hotspot).
class SetupPortal {
private:
    WebServer server{SETUP_HTTP_PORT};
    StorageManager* storage = nullptr;
    NetworkManager* network = nullptr;

    static const char* setupPageHtml() {
        return R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gateway Wi-Fi Setup</title>
<style>
body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:20px}
.card{max-width:420px;margin:auto;background:#1e293b;border-radius:10px;padding:20px}
h1{font-size:20px;margin:0 0 14px}
label{display:block;margin:12px 0 4px;font-size:13px;color:#94a3b8}
input,select{width:100%;padding:10px;border-radius:6px;border:1px solid #334155;background:#0f172a;color:#e2e8f0;box-sizing:border-box}
button{margin-top:16px;width:100%;padding:12px;border:0;border-radius:6px;background:#22c55e;color:#052e16;font-weight:700;cursor:pointer}
button.alt{background:#334155;color:#e2e8f0;margin-top:8px}
#msg{margin-top:12px;font-size:14px;min-height:18px}
</style></head><body><div class="card">
<h1>Wi-Fi Setup</h1>
<p style="font-size:13px;color:#94a3b8;margin-top:0">Connect this device to your internet Wi-Fi. This page works only while you are connected to the device hotspot.</p>
<label for="list">Networks found</label>
<select id="list" onchange="document.getElementById('ssid').value=this.value"><option value="">-- scan first --</option></select>
<button class="alt" onclick="scan()">Scan networks</button>
<label for="ssid">Wi-Fi name (SSID)</label>
<input id="ssid" maxlength="32" autocomplete="off">
<label for="pass">Wi-Fi password</label>
<input id="pass" type="password" maxlength="64" autocomplete="off">
<button onclick="save()">Save and connect</button>
<div id="msg"></div>
</div>
<script>
function say(t,ok){var m=document.getElementById('msg');m.textContent=t;m.style.color=ok?'#22c55e':'#f87171';}
async function scan(){
  say('Scanning...',true);
  try{
    var r=await fetch('/scan');
    if(!r.ok){say('Scan failed ('+r.status+')',false);return;}
    var list=await r.json();var s=document.getElementById('list');
    s.innerHTML='<option value="">-- choose --</option>';
    list.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';s.appendChild(o);});
    say('Found '+list.length+' networks',true);
  }catch(e){say('Scan error',false);}
}
async function save(){
  var body=new URLSearchParams({ssid:document.getElementById('ssid').value,pass:document.getElementById('pass').value});
  try{
    var r=await fetch('/wifi',{method:'POST',body:body});
    say(r.ok?'Saved. The device is connecting to Wi-Fi...':'Save failed ('+r.status+')',r.ok);
  }catch(e){say('Save error',false);}
}
</script></body></html>)HTML";
    }

    // Refuses LAN access outright; returns false after sending the refusal
    bool hotspotOnly() {
        if (SecurityUtils::requestFromHotspot(server)) return true;
        server.send(403, "text/plain", "Available only on the device hotspot");
        return false;
    }

    bool adminOnly() {
        DeviceSettings& s = storage->getSettings();
        if (server.authenticate(s.adminUser, s.adminPass)) return true;
        server.requestAuthentication(BASIC_AUTH, "Gateway Admin");
        return false;
    }

    void handlePage() {
        if (!hotspotOnly() || !adminOnly()) return;
        server.send(200, "text/html", setupPageHtml());
    }

    void handleScan() {
        if (!hotspotOnly() || !adminOnly()) return;
        int n = WiFi.scanNetworks();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject net = arr.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
        }
        WiFi.scanDelete();
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    }

    void handleSave() {
        if (!hotspotOnly() || !adminOnly()) return;
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
            server.send(400, "text/plain", "Invalid Wi-Fi name or password");
            return;
        }
        DeviceSettings& s = storage->getSettings();
        strncpy(s.staSSID, ssid.c_str(), sizeof(s.staSSID) - 1);
        strncpy(s.staPass, pass.c_str(), sizeof(s.staPass) - 1);
        storage->saveSettings();
        network->setCredentials(ssid, pass);
        Serial.printf("[SETUP] Wi-Fi saved from hotspot for '%s'.\n", ssid.c_str());
        server.send(200, "text/plain", "Saved");
    }

public:
    SetupPortal() {}

    void begin(StorageManager* stor, NetworkManager* net) {
        storage = stor;
        network = net;
        // authenticate() only sees the Authorization header when it is collected
        const char* headerKeys[] = { "Authorization" };
        server.collectHeaders(headerKeys, 1);
        server.on("/", HTTP_GET, [this]() { handlePage(); });
        server.on("/scan", HTTP_GET, [this]() { handleScan(); });
        server.on("/wifi", HTTP_POST, [this]() { handleSave(); });
        server.onNotFound([this]() {
            if (!hotspotOnly()) return;
            server.send(404, "text/plain", "Not found");
        });
        server.begin();
        Serial.printf("[SETUP] Hotspot-only Wi-Fi setup on port %d\n", SETUP_HTTP_PORT);
    }

    void update() {
        server.handleClient();
    }
};

#endif // SETUP_PORTAL_H
