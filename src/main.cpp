#include <Arduino.h>
#include "Config.h"
#include "StorageManager.h"
#include "QRCodeRenderer.h"
#include "DisplayManager.h"
#include "RelayController.h"
#include "NetworkManager.h"
#include "PaymentClient.h"
#include "WebServerManager.h"
#include "esp_task_wdt.h"

// System Components
StorageManager storage;
DisplayManager display;
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
unsigned long successStartTime = 0;
bool successScreenReturned = false;
bool lastWifiState = false;

// Physical Trigger Button (D25)
#define BUTTON_TRIGGER_PIN DEFAULT_PAYMENT_TRIGGER_PIN

// Function Prototypes
bool triggerNewPayment(float amount, uint32_t duration, String& outError);
void cancelActivePayment();
void checkPhysicalButton();
void handlePhysicalButtonTrigger();

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

    // 3. Initialize Physical Start Button (D25, Active LOW with Internal Pull-up)
    pinMode(BUTTON_TRIGGER_PIN, INPUT_PULLUP);
    Serial.printf("[MAIN] Physical trigger button initialized on GPIO %d (Active LOW)\n", BUTTON_TRIGGER_PIN);

    // 4. Initialize 2.8-inch SPI TFT Display (ILI9341 240x320)
    display.begin(s);

    // 5. Initialize Network (AP + Station)
    network.begin(s);

    // 6. Initialize Payment Client
    paymentClient.begin(s.backendUrl, s.deviceKey);

    // 7. Set Web Server Callbacks and Start Server
    webServer.setPaymentCallbacks(triggerNewPayment, cancelActivePayment);
    webServer.begin(&storage, &relay, &network);

    delay(1000);

    // 8. Ready State -> SHOW ONLY HOME PAGE
    relay.setState(STATE_READY);
    lastWifiState = network.isConnectedToInternet();
    display.showHomeScreen(String(s.apSSID), network.getStaIP(), lastWifiState, 
                           s.ruleCount, s.rules);

    Serial.printf("[MAIN] System ready. Admin Hotspot: '%s' (http://192.168.4.1)\n", s.apSSID);
    Serial.printf("[MAIN] Display Mode: HOME -> QR+TIMER -> SUCCESS/FAILED only.\n");
}

void loop() {
    // 1. Maintain background services
    network.update();
    webServer.update();

    // 2. Check physical trigger button (GPIO 25 LOW)
    checkPhysicalButton();

    // 3. Check Relay Timer (Non-blocking)
    bool relayFinished = relay.update();
    if (relayFinished) {
        // Relay has completed its cycle - show closed status then return to home screen
        relay.setState(STATE_READY);
        storage.logTransaction(activeQrId.c_str(), "CAPTURED", activeAmount, activeDuration, true);
        webServer.clearActivePaymentSession();

        display.updatePaymentSuccessRelayStatus(0);
        delay(600);

        DeviceSettings& s = storage.getSettings();
        display.showHomeScreen(String(s.apSSID), network.getStaIP(), 
                               network.isConnectedToInternet(), s.ruleCount, s.rules);
    }

    GatewayState currentState = relay.getState();

    // 4. State Machine Handling
    switch (currentState) {
        case STATE_READY: {
            // Update home page if Wi-Fi connection status changes
            bool curWifi = network.isConnectedToInternet();
            if (curWifi != lastWifiState || (millis() - lastDisplayUpdate > 10000)) {
                lastWifiState = curWifi;
                lastDisplayUpdate = millis();
                DeviceSettings& s = storage.getSettings();
                display.showHomeScreen(String(s.apSSID), network.getStaIP(), 
                                       lastWifiState, s.ruleCount, s.rules);
            }
            break;
        }

        case STATE_QR_ACTIVE: {
            unsigned long now = millis();
            unsigned long elapsedSec = (now - qrStartTime) / 1000;

            // Check if QR payment expired
            if (elapsedSec >= QR_EXPIRY_SECONDS) {
                Serial.println("[MAIN] QR payment session expired.");
                relay.setState(STATE_ERROR);
                errorStartTime = millis();
                display.showPaymentFailedScreen("Payment Expired");
                webServer.clearActivePaymentSession();
                break;
            }

            // Update remaining time on display every second (flicker-free)
            if (now - lastDisplayUpdate >= 1000) {
                lastDisplayUpdate = now;
                uint32_t remainingSec = (elapsedSec < QR_EXPIRY_SECONDS) ? (QR_EXPIRY_SECONDS - elapsedSec) : 0;
                display.updateQRCountdown(remainingSec);
            }

            // Poll backend verification endpoint
            if (now - lastPollTime >= STATUS_POLL_INTERVAL_MS) {
                lastPollTime = now;
                String status, paymentId, authToken, err;
                
                if (paymentClient.checkPaymentStatus(activeQrId, status, paymentId, authToken, err)) {
                    if (status == "captured") {
                        Serial.printf("[MAIN] Payment CAPTURED! ID=%s. Authorizing relay for %us...\n", 
                                      paymentId.c_str(), activeDuration);

                        // Anti-replay verification
                        if (storage.isPaymentConsumed(paymentId.c_str())) {
                            Serial.println("[SECURITY ALERT] Duplicate payment token detected! Rejecting replay.");
                            relay.setState(STATE_ERROR);
                            errorStartTime = millis();
                            display.showPaymentFailedScreen("Duplicate Token");
                            break;
                        }

                        // Inform backend to permanently consume session
                        paymentClient.consumePayment(activeQrId, paymentId, authToken);

                        relay.setState(STATE_PAID);

                        // Activate Relay (3 seconds by default)
                        relay.activate(activeDuration);
                        relay.setState(STATE_RELAY_ON);

                        // SHOW PAYMENT SUCCESSFUL SCREEN WITH LIVE RELAY STATUS
                        display.showPaymentSuccessScreen(activeAmount, activeDuration, relay.getRemainingSeconds());
                        lastDisplayUpdate = millis();
                    } else if (status == "expired") {
                        relay.setState(STATE_ERROR);
                        errorStartTime = millis();
                        display.showPaymentFailedScreen("Session Expired");
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
            // Live countdown update of relay status on display while open
            if (millis() - lastDisplayUpdate >= 1000) {
                lastDisplayUpdate = millis();
                uint32_t rem = relay.getRemainingSeconds();
                display.updatePaymentSuccessRelayStatus(rem);
            }
            break;
        }

        case STATE_ERROR: {
            // Keep Payment Failed screen visible for 3 seconds, then return to Home Page
            if (millis() - errorStartTime >= 3000) {
                activeQrId = "";
                activeQrPayload = "";
                activeAmount = 0.0f;
                activeDuration = 0;
                relay.setState(STATE_READY);
                DeviceSettings& s = storage.getSettings();
                display.showHomeScreen(String(s.apSSID), network.getStaIP(), 
                                       network.isConnectedToInternet(), s.ruleCount, s.rules);
            }
            break;
        }

        default:
            break;
    }

    yield();
}

// Handler to trigger new payment request (called from Admin Portal or physical D25 button)
bool triggerNewPayment(float amount, uint32_t duration, String& outError) {
    if (amount <= 0.0f) {
        outError = "Invalid amount";
        return false;
    }

    DeviceSettings& s = storage.getSettings();
    if (duration == 0) {
        duration = storage.getDurationForAmount(amount);
    }

    // Call Cloud Backend to create QR session
    String qrId, qrPayload;
    bool ok = paymentClient.createQRSession(amount, duration, qrId, qrPayload, outError);

    if (!ok) {
        Serial.printf("[MAIN] Failed to create payment QR: %s\n", outError.c_str());
        relay.setState(STATE_ERROR);
        errorStartTime = millis();
        // SHOW PAYMENT FAILED SCREEN
        display.showPaymentFailedScreen(outError);
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

    // SHOW QR CODE AND TIMER SCREEN
    display.showQRScreen(activeQrPayload, activeAmount, activeDuration, activeQrId, QR_EXPIRY_SECONDS);

    Serial.printf("[MAIN] QR Code displayed on 2.8\" TFT. ID=%s, Amount=Rs. %.2f, Duration=%us\n",
                  activeQrId.c_str(), activeAmount, activeDuration);

    return true;
}

void cancelActivePayment() {
    activeQrId = "";
    activeQrPayload = "";
    activeAmount = 0.0f;
    activeDuration = 0;
    relay.setState(STATE_READY);
    DeviceSettings& s = storage.getSettings();
    display.showHomeScreen(String(s.apSSID), network.getStaIP(), 
                           network.isConnectedToInternet(), s.ruleCount, s.rules);
}

// Check physical push button connected to GPIO 25 (Active LOW with debounce)
void checkPhysicalButton() {
    static int lastReading = HIGH;
    static int buttonState = HIGH;
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50; // 50ms debounce time

    int reading = digitalRead(BUTTON_TRIGGER_PIN);

    if (reading != lastReading) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (reading != buttonState) {
            buttonState = reading;

            // Trigger when button is pressed (becomes LOW)
            if (buttonState == LOW) {
                handlePhysicalButtonTrigger();
            }
        }
    }

    lastReading = reading;
}

// Initiate new payment when D25 becomes LOW (similar to webpage start button)
void handlePhysicalButtonTrigger() {
    GatewayState currentState = relay.getState();

    // If relay is currently energized, ignore trigger
    if (currentState == STATE_RELAY_ON) {
        Serial.println("[BUTTON] D25 pressed, but relay is currently active. Ignoring.");
        return;
    }

    // If a QR payment session is already active, ignore to avoid accidental cancellation
    if (currentState == STATE_QR_ACTIVE) {
        Serial.printf("[BUTTON] D25 pressed, but QR session is already active (ID: %s). Ignoring.\n", activeQrId.c_str());
        return;
    }

    // If device is in error state, reset to ready first
    if (currentState == STATE_ERROR) {
        relay.setState(STATE_READY);
    }

    // Retrieve default payment rule (Rule 0 configured in NVS / Web Portal: Default 1 Rupee for 3 seconds)
    DeviceSettings& s = storage.getSettings();
    float amount = 1.0f;
    uint32_t duration = 3;

    if (s.ruleCount > 0) {
        amount = s.rules[0].amount;
        duration = s.rules[0].durationSec;
    }

    Serial.printf("[BUTTON] D25 went LOW! Initiating payment for configured rule: Rs. %.2f (%u sec)...\n", 
                  amount, duration);

    String err;
    if (!triggerNewPayment(amount, duration, err)) {
        Serial.printf("[BUTTON] Failed to initiate payment: %s\n", err.c_str());
    }
}
