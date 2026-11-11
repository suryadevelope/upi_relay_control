#ifndef PAYMENT_CLIENT_H
#define PAYMENT_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "Config.h"

class PaymentClient {
private:
    String backendUrl;
    String deviceKey;

    // Bind HTTPClient to the right transport. The caller owns both client
    // objects so they stay alive for the whole request.
    static bool beginRequest(HTTPClient& http, WiFiClient& client,
                             WiFiClientSecure& secureClient, const String& endpoint) {
        if (endpoint.startsWith("https://")) {
            secureClient.setInsecure(); // For custom/embedded SSL, skip CA check
            return http.begin(secureClient, endpoint);
        }
        return http.begin(client, endpoint);
    }

public:
    PaymentClient() {}

    void begin(const String& url, const String& key) {
        backendUrl = url;
        deviceKey = key;
        // Trim trailing slash if present
        if (backendUrl.endsWith("/")) {
            backendUrl = backendUrl.substring(0, backendUrl.length() - 1);
        }
        Serial.printf("[CLIENT] Initialized with backend: %s\n", backendUrl.c_str());
    }

    void setConfig(const String& url, const String& key) {
        backendUrl = url;
        deviceKey = key;
        if (backendUrl.endsWith("/")) {
            backendUrl = backendUrl.substring(0, backendUrl.length() - 1);
        }
    }

    // Call Cloud Backend to initiate Razorpay UPI QR creation
    bool createQRSession(float amount, uint32_t durationSec, 
                         String& outQrId, String& outQrPayload, String& outError) {
        if (WiFi.status() != WL_CONNECTED) {
            outError = "No Internet connection";
            return false;
        }

        HTTPClient http;
        String endpoint = backendUrl + "/api/payment/create-qr";

        // Clients must outlive the HTTPClient that references them
        WiFiClient client;
        WiFiClientSecure secureClient;
        if (!beginRequest(http, client, secureClient, endpoint)) {
            outError = "HTTP init failed";
            return false;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Device-Key", deviceKey);
        http.setTimeout(20000); // QR creation takes 5-9 s through the cloud; 10 s was too tight

        JsonDocument reqDoc;
        reqDoc["amount"] = amount;
        reqDoc["duration"] = durationSec;
        reqDoc["deviceId"] = WiFi.macAddress();
        reqDoc["description"] = "Smart Gateway Access";

        String reqBody;
        serializeJson(reqDoc, reqBody);

        int httpCode = http.POST(reqBody);
        if (httpCode <= 0) {
            outError = "HTTP error: " + http.errorToString(httpCode);
            http.end();
            return false;
        }

        String resBody = http.getString();
        http.end();

        if (httpCode != 200 && httpCode != 201) {
            outError = "Server HTTP " + String(httpCode) + ": " + resBody;
            return false;
        }

        JsonDocument resDoc;
        DeserializationError err = deserializeJson(resDoc, resBody);
        if (err) {
            outError = "JSON parse error: " + String(err.c_str());
            return false;
        }

        if (!resDoc["success"].as<bool>()) {
            outError = resDoc["error"].as<String>();
            return false;
        }

        outQrId = resDoc["qr_id"].as<String>();
        outQrPayload = resDoc["qr_payload"].as<String>();

        // Use Razorpay's UPI intent unmodified: its pa=/tr= route the payment
        // to the Razorpay QR so the backend can see it captured. Never
        // substitute or rewrite the VPA here.
        if (!outQrPayload.startsWith("upi://pay") || outQrPayload.indexOf("pa=") == -1) {
            Serial.printf("[CLIENT] Backend returned no valid UPI payload. Response: %s\n",
                          resBody.c_str());
            outError = "Backend returned no valid UPI payload (qr_payload)";
            return false;
        }
        if (outQrPayload.indexOf("pa=random@razorpay") != -1) {
            Serial.println("[CLIENT] WARNING: Razorpay TEST-mode QR (pa=random@razorpay). "
                           "Real UPI apps cannot pay it - use live API keys on the backend.");
        }

        Serial.printf("[CLIENT] QR session created: ID=%s, Payload=%s\n",
                      outQrId.c_str(), outQrPayload.c_str());
        return true;
    }

    // Poll backend to verify if payment was captured
    bool checkPaymentStatus(const String& qrId, String& outStatus, 
                            String& outPaymentId, String& outAuthToken, String& outError) {
        if (WiFi.status() != WL_CONNECTED) {
            outError = "No Internet connection";
            return false;
        }

        HTTPClient http;
        String endpoint = backendUrl + "/api/payment/status/" + qrId;

        // Clients must outlive the HTTPClient that references them
        WiFiClient client;
        WiFiClientSecure secureClient;
        if (!beginRequest(http, client, secureClient, endpoint)) {
            outError = "HTTP init failed";
            return false;
        }

        http.addHeader("X-Device-Key", deviceKey);
        http.setTimeout(5000);

        int httpCode = http.GET();
        if (httpCode <= 0) {
            outError = "HTTP error: " + http.errorToString(httpCode);
            http.end();
            return false;
        }

        String resBody = http.getString();
        http.end();

        if (httpCode != 200) {
            outError = "Status check HTTP " + String(httpCode) + ": " + resBody;
            return false;
        }

        JsonDocument resDoc;
        DeserializationError err = deserializeJson(resDoc, resBody);
        if (err) {
            outError = "JSON parse error: " + String(err.c_str());
            return false;
        }

        outStatus = resDoc["status"].as<String>(); // "pending", "captured", "consumed", "expired"
        outPaymentId = resDoc["payment_id"].as<String>();
        outAuthToken = resDoc["activation_token"].as<String>();

        return true;
    }

    // Authorize and consume payment session to prevent replay
    bool consumePayment(const String& qrId, const String& paymentId, const String& authToken) {
        if (WiFi.status() != WL_CONNECTED) return false;

        HTTPClient http;
        String endpoint = backendUrl + "/api/payment/consume/" + qrId;

        WiFiClient client;
        WiFiClientSecure secureClient;
        if (!beginRequest(http, client, secureClient, endpoint)) return false;

        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Device-Key", deviceKey);
        http.setTimeout(5000);

        JsonDocument reqDoc;
        reqDoc["paymentId"] = paymentId;
        reqDoc["token"] = authToken;
        reqDoc["deviceId"] = WiFi.macAddress();

        String reqBody;
        serializeJson(reqDoc, reqBody);

        int httpCode = http.POST(reqBody);
        http.end();

        return (httpCode == 200);
    }
};

#endif // PAYMENT_CLIENT_H
