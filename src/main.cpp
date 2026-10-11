#include <Arduino.h>
#include "Config.h"
#include "StorageManager.h"
#include "QRCodeRenderer.h"
// #include "DisplayManager.h"  // Disabled for now - testing payment flow
#include "RelayController.h"
#include "NetworkManager.h"
#include "PaymentClient.h"
#include "WebServerManager.h"
#include "esp_task_wdt.h"  // For watchdog feeding

// System Components
StorageManager storage;
// DisplayManager display;  // Disabled for now
RelayController relay;
NetworkManager network;
PaymentClient paymentClient;
WebServerManager webServer;

// Active Payment Session State
String activeQrId = "";
String activeQrPayload = "";
float activeAmount = 0.0f;
uint32_t activeDuration = 0;
unsigned long qrStartTime = 0;
unsigned long lastPollTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long errorStartTime = 0;
unsigned long completeStartTime = 0;
bool lastWifiState = false;

// Function Prototypes
bool triggerNewPayment(float amount, uint32_t duration, String& outError);
void cancelActivePayment();

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n========================================================");
    Serial.println(" SMART PAYMENT GATEWAY - ESP32 FIRMWARE BOOTING");
    Serial.println("========================================================");

    // 1. Initialize Storage (NVS)
    storage.begin();
    DeviceSettings& s = storage.getSettings();

    // 2. Initialize Relay Controller (Fail-safe OFF)
    relay.begin(s.relayPin, s.relayActiveHigh);

    // 3. Initialize Display - DISABLED for now
    // display.begin(s);
    // display.showBootScreen();

    // 4. Initialize Network (AP + STA)
    network.begin(s);

    // 5. Initialize Payment Client
    paymentClient.begin(s.backendUrl, s.deviceKey);

    // 6. Set Web Server Callbacks and Start Server
    webServer.setPaymentCallbacks(triggerNewPayment, cancelActivePayment);
    webServer.begin(&storage, &relay, &network);

    delay(1500);

    // 7. Ready State
    relay.setState(STATE_READY);
    lastWifiState = network.isConnectedToInternet();
    // display.showReadyScreen(String(s.apSSID), network.getStaIP(), lastWifiState, 
    //                        s.ruleCount, s.rules);

    Serial.printf("[MAIN] System initialized. Debug mode: %s\n", s.debugMode ? "ENABLED" : "DISABLED");
    Serial.printf("[MAIN] Admin Hotspot: '%s' (http://192.168.4.1)\n", s.apSSID);
}

void loop() {
    // 1. Maintain background services
    network.update();
    webServer.update();

    // 2. Check Relay Timer (Non-blocking)
    bool relayFinished = relay.update();
    if (relayFinished) {
        completeStartTime = millis();
        // display.showCompleteScreen();  // Disabled
        // Record successful transaction in NVS
        storage.logTransaction(activeQrId.c_str(), "CAPTURED", activeAmount, activeDuration, true);
        webServer.clearActivePaymentSession();
    }

    GatewayState currentState = relay.getState();

    // 3. State Machine Handling
    switch (currentState) {
        case STATE_READY: {
            // Update display if Wi-Fi state changed
            bool curWifi = network.isConnectedToInternet();
            if (curWifi != lastWifiState || (millis() - lastDisplayUpdate > 10000)) {
                lastWifiState = curWifi;
                lastDisplayUpdate = millis();
                DeviceSettings& s = storage.getSettings();
                // display.showReadyScreen(String(s.apSSID), network.getStaIP(), 
                //                        lastWifiState, s.ruleCount, s.rules);
            }
            break;
        }

        case STATE_QR_ACTIVE: {
            unsigned long now = millis();
            unsigned long elapsedSec = (now - qrStartTime) / 1000;

            // Check if QR expired
            if (elapsedSec >= QR_EXPIRY_SECONDS) {
                Serial.println("[MAIN] QR payment session expired.");
                relay.setState(STATE_ERROR);
                errorStartTime = millis();
                // display.showErrorScreen("Payment Expired (Timeout)");
                webServer.clearActivePaymentSession();
                break;
            }

            // Update remaining time on display every second
            if (now - lastDisplayUpdate >= 1000) {
                lastDisplayUpdate = now;
                uint32_t remainingSec = QR_EXPIRY_SECONDS - elapsedSec;
                // display.showQRScreen(activeQrPayload, activeAmount, activeDuration, 
                //                      activeQrId, remainingSec);
            }

            // Poll backend verification endpoint
            if (now - lastPollTime >= STATUS_POLL_INTERVAL_MS) {
                lastPollTime = now;
                String status, paymentId, authToken, err;
                
                if (paymentClient.checkPaymentStatus(activeQrId, status, paymentId, authToken, err)) {
                    if (status == "captured") {
                        Serial.printf("[MAIN] Payment CAPTURED! ID=%s. Authorizing relay...\n", paymentId.c_str());

                        // Anti-replay verification
                        if (storage.isPaymentConsumed(paymentId.c_str())) {
                            Serial.println("[SECURITY ALERT] Duplicate payment token detected! Rejecting replay.");
                            relay.setState(STATE_ERROR);
                            errorStartTime = millis();
                            // display.showErrorScreen("Duplicate Payment Replay");
                            break;
                        }

                        // Inform backend to permanently consume session
                        paymentClient.consumePayment(activeQrId, paymentId, authToken);

                        relay.setState(STATE_PAID);
                        // display.showPaymentSuccess(activeAmount, activeDuration);
                        delay(1200); // Brief visual confirmation

                        // Activate Relay
                        relay.activate(activeDuration);
                        // display.showRelayActiveScreen(activeDuration, activeDuration, activeAmount);
                    } else if (status == "expired") {
                        relay.setState(STATE_ERROR);
                        errorStartTime = millis();
                        // display.showErrorScreen("Session Expired on Server");
                        webServer.clearActivePaymentSession();
                    }
                } else {
                    if (storage.getSettings().debugMode) {
                        Serial.printf("[DEBUG] Poll status note: %s\n", err.c_str());
                    }
                }
            }
            break;
        }

        case STATE_RELAY_ON: {
            // Update active relay countdown every second
            if (millis() - lastDisplayUpdate >= 1000) {
                lastDisplayUpdate = millis();
                uint32_t rem = relay.getRemainingSeconds();
                uint32_t tot = relay.getTotalDurationSeconds();
                // display.showRelayActiveScreen(rem, tot, activeAmount);
            }
            break;
        }

        case STATE_COMPLETE: {
            // Wait 3 seconds on complete screen before returning to ready
            if (millis() - completeStartTime >= 3000) {
                activeQrId = "";
                activeQrPayload = "";
                activeAmount = 0.0f;
                activeDuration = 0;
                relay.setState(STATE_READY);
                DeviceSettings& s = storage.getSettings();
                // display.showReadyScreen(String(s.apSSID), network.getStaIP(), 
                //                        network.isConnectedToInternet(), s.ruleCount, s.rules);
            }
            break;
        }

        case STATE_ERROR: {
            // Wait 4 seconds on error screen before returning to ready
            if (millis() - errorStartTime >= 4000) {
                relay.setState(STATE_READY);
                DeviceSettings& s = storage.getSettings();
                // display.showReadyScreen(String(s.apSSID), network.getStaIP(), 
                //                        network.isConnectedToInternet(), s.ruleCount, s.rules);
            }
            break;
        }

        default:
            break;
    }

    yield();
}

// Handler to trigger new payment request (called from Admin Portal or physical button)
bool triggerNewPayment(float amount, uint32_t duration, String& outError) {
    if (amount <= 0.0f) {
        outError = "Invalid amount";
        return false;
    }

    DeviceSettings& s = storage.getSettings();
    if (duration == 0) {
        duration = storage.getDurationForAmount(amount);
    }

    // Call Cloud Backend
    // display.showVerifyingScreen(amount);  // Disabled
    String qrId, qrPayload;
    bool ok = paymentClient.createQRSession(amount, duration, qrId, qrPayload, outError);

    if (!ok) {
        Serial.printf("[MAIN] Failed to create payment QR: %s\n", outError.c_str());
        relay.setState(STATE_ERROR);
        errorStartTime = millis();
        // display.showErrorScreen(outError);
        return false;
    }

    activeQrId = qrId;
    activeQrPayload = qrPayload;
    activeAmount = amount;
    activeDuration = duration;
    qrStartTime = millis();
    lastPollTime = millis();
    lastDisplayUpdate = millis();

    // Inform Web Admin
    webServer.setActivePaymentSession(activeQrId, activeQrPayload, activeAmount, activeDuration);

    // Update State
    relay.setState(STATE_QR_ACTIVE);

    // Render on TFT Display - DISABLED
    // display.showQRScreen(activeQrPayload, activeAmount, activeDuration, activeQrId, QR_EXPIRY_SECONDS);

    // =========================================================================
    // USER REQUIREMENT: IF DEBUG MODE IS ENABLED, PRINT QR CODE TO SERIAL
    // =========================================================================
    if (s.debugMode) {
        // Feed watchdog before long serial printing
        esp_task_wdt_reset();
        QRCodeRenderer::printToSerial(activeQrPayload, activeAmount, activeDuration, activeQrId);
        esp_task_wdt_reset();
    }

    return true;
}

void cancelActivePayment() {
    activeQrId = "";
    activeQrPayload = "";
    activeAmount = 0.0f;
    activeDuration = 0;
    relay.setState(STATE_READY);
    DeviceSettings& s = storage.getSettings();
    // display.showReadyScreen(String(s.apSSID), network.getStaIP(), 
    //                        network.isConnectedToInternet(), s.ruleCount, s.rules);
}

