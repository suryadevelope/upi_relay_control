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

// Selected payment rule (short press cycles this on the home screen)
int selectedRuleIndex = 0;

// Single Physical Control Button (D25)
// - Short press: cycle to the next configured amount
// - Long press : start payment for the selected amount
#define BUTTON_TRIGGER_PIN DEFAULT_PAYMENT_TRIGGER_PIN
#define BUTTON_DEBOUNCE_MS 40
#define BUTTON_LONG_PRESS_MS 1500

// Function Prototypes
bool triggerNewPayment(float amount, uint32_t duration, String& outError);
void cancelActivePayment();
void normalizeSelectedRule();
void refreshHomeScreen();
void checkPhysicalButton();
void handleButtonShortPress();
void handleButtonLongPress();
void startSelectedPayment();

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

    // 3. Initialize Single Physical Button (D25, Active LOW with Internal Pull-up)
    pinMode(BUTTON_TRIGGER_PIN, INPUT_PULLUP);
    Serial.printf("[MAIN] Single button initialized on GPIO %d (Active LOW)\n", BUTTON_TRIGGER_PIN);
    Serial.println("[MAIN] Button controls: SHORT press = next amount, LONG press (1.5s) = start payment");

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
    selectedRuleIndex = 0;
    refreshHomeScreen();

    Serial.printf("[MAIN] System ready. Admin Hotspot: '%s' (http://192.168.4.1)\n", s.apSSID);
    Serial.printf("[MAIN] Display Mode: HOME -> QR+TIMER -> SUCCESS/FAILED only.\n");
}

void loop() {
    // 1. Maintain background services
    network.update();
    webServer.update();

    // Apply backend URL/key changes saved from the admin portal without rebooting
    if (webServer.consumeSettingsChanged()) {
        DeviceSettings& cfg = storage.getSettings();
        paymentClient.setConfig(cfg.backendUrl, cfg.deviceKey);
        Serial.printf("[MAIN] Backend config applied: %s\n", cfg.backendUrl);
    }

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
        refreshHomeScreen();
    }

    GatewayState currentState = relay.getState();

    // 4. State Machine Handling
    switch (currentState) {
        case STATE_READY: {
            // Refresh home page if Wi-Fi status changes (also picks up rule changes)
            bool curWifi = network.isConnectedToInternet();
            if (curWifi != lastWifiState || (millis() - lastDisplayUpdate > 10000)) {
                refreshHomeScreen();
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
                refreshHomeScreen();
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
    refreshHomeScreen();
}

// Keep the selected rule inside the configured range (rules can change from the web UI)
void normalizeSelectedRule() {
    DeviceSettings& s = storage.getSettings();
    if (s.ruleCount <= 0) {
        selectedRuleIndex = 0;
        return;
    }
    if (selectedRuleIndex < 0) selectedRuleIndex = 0;
    if (selectedRuleIndex >= s.ruleCount) selectedRuleIndex = s.ruleCount - 1;
}

// Redraw the home page with the currently selected amount
void refreshHomeScreen() {
    normalizeSelectedRule();
    DeviceSettings& s = storage.getSettings();
    bool connected = network.isConnectedToInternet();
    lastWifiState = connected;
    lastDisplayUpdate = millis();
    display.showHomeScreen(String(s.apSSID), network.getStaIP(), connected,
                           s.ruleCount, s.rules, selectedRuleIndex);
}

// Non-blocking button reader: debounces, detects short and long presses.
// A long press fires once while the button is still held.
void checkPhysicalButton() {
    static int lastReading = HIGH;
    static bool stablePressed = false;
    static bool longPressHandled = false;
    static unsigned long lastChangeTime = 0;
    static unsigned long pressStartTime = 0;

    int reading = digitalRead(BUTTON_TRIGGER_PIN);
    unsigned long now = millis();

    if (reading != lastReading) {
        lastReading = reading;
        lastChangeTime = now;
        return;
    }

    if (now - lastChangeTime < BUTTON_DEBOUNCE_MS) return;

    bool pressed = (reading == LOW);

    if (pressed && !stablePressed) {
        // Fresh press
        stablePressed = true;
        longPressHandled = false;
        pressStartTime = now;
    } else if (pressed && stablePressed) {
        // Still held: fire the long press exactly once at the threshold
        if (!longPressHandled && (now - pressStartTime >= BUTTON_LONG_PRESS_MS)) {
            longPressHandled = true;
            handleButtonLongPress();
        }
    } else if (!pressed && stablePressed) {
        // Released: a short press advances the amount selection
        stablePressed = false;
        bool wasLongPress = longPressHandled;
        longPressHandled = false;
        if (!wasLongPress) {
            handleButtonShortPress();
        }
    }
}

// Short press: cycle through the configured amount/duration rules
void handleButtonShortPress() {
    GatewayState currentState = relay.getState();
    if (currentState != STATE_READY) {
        Serial.printf("[BUTTON] Short press ignored in state %s\n", stateToString(currentState));
        return;
    }

    DeviceSettings& s = storage.getSettings();
    if (s.ruleCount <= 1) {
        Serial.println("[BUTTON] Short press ignored: only one amount is configured.");
        return;
    }

    selectedRuleIndex = (selectedRuleIndex + 1) % s.ruleCount;
    refreshHomeScreen();

    Serial.printf("[BUTTON] Selected amount %d/%d: Rs. %.2f for %u sec\n",
                  selectedRuleIndex + 1, s.ruleCount,
                  s.rules[selectedRuleIndex].amount,
                  s.rules[selectedRuleIndex].durationSec);
}

// Long press: start payment for the amount currently shown on the display
void handleButtonLongPress() {
    GatewayState currentState = relay.getState();
    if (currentState != STATE_READY) {
        Serial.printf("[BUTTON] Long press ignored in state %s\n", stateToString(currentState));
        return;
    }
    startSelectedPayment();
}

void startSelectedPayment() {
    normalizeSelectedRule();
    DeviceSettings& s = storage.getSettings();

    float amount = 1.0f;
    uint32_t duration = 3;
    if (s.ruleCount > 0) {
        amount = s.rules[selectedRuleIndex].amount;
        duration = s.rules[selectedRuleIndex].durationSec;
    }

    Serial.printf("[BUTTON] Long press detected. Starting payment: Rs. %.2f for %u sec...\n",
                  amount, duration);

    String err;
    if (!triggerNewPayment(amount, duration, err)) {
        Serial.printf("[BUTTON] Failed to initiate payment: %s\n", err.c_str());
    }
}
