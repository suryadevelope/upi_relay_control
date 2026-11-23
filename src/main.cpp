#include <Arduino.h>
#include "Config.h"
#include "StorageManager.h"
#include "QRCodeRenderer.h"
#include "DisplayManager.h"
#include "RelayController.h"
#include "NetworkManager.h"
#include "PaymentClient.h"
#include "ButtonManager.h"
#include "DiscoveryService.h"
#include "AdminApi.h"
#include "SetupPortal.h"
#include "esp_task_wdt.h"

// System components
StorageManager storage;
DisplayManager display;
RelayController relay;
NetworkManager network;
PaymentClient paymentClient;
ButtonManager buttons;
DiscoveryService discovery;
AdminApi adminApi;
SetupPortal setupPortal;
RuntimeStatus runtime;

// Menu screens shown while the gateway is READY (payment states use GatewayState)
enum MenuScreen {
    MENU_HOME,      // Amount card list (UP/DOWN select, ENTER pay, RIGHT info)
    MENU_CONFIRM,   // Confirm the selected card (ENTER show QR, LEFT back)
    MENU_INFO,      // Device info (LEFT back)
    MENU_NOTICE,    // Short message; returns to HOME automatically
    MENU_LOADING    // Waiting for the cloud to create the QR (buttons locked)
};
MenuScreen menuScreen = MENU_HOME;
unsigned long noticeUntil = 0;

// QR request running on a background task, so the loading bar keeps animating
volatile bool qrRequestBusy = false;
volatile bool qrRequestDone = false;
volatile bool qrRequestCancelled = false;
bool qrReqOk = false;
String qrReqId, qrReqPayload, qrReqError;
float qrReqAmount = 0.0f;
uint32_t qrReqDuration = 0;
unsigned long qrRequestStart = 0;
unsigned long lastLoadingFrame = 0;

// Active payment session
String activeQrId = "";
String activeQrPayload = "";
float activeAmount = 0.0f;
uint32_t activeDuration = 0;
unsigned long qrStartTime = 0;
unsigned long lastPollTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long errorStartTime = 0;
unsigned long stateEnteredTime = 0;
bool relayIsPayment = false;   // false while the admin is running a relay test
bool mode2Active = false;      // true while a mode-2 (fixed 5s) relay cycle runs

int selectedRuleIndex = 0;
bool lastWifiState = false;
bool configDirty = false;

// Function prototypes
bool triggerNewPayment(float amount, uint32_t duration, String& outError);
void cancelActivePayment();
void resetPaymentSession();
void normalizeSelectedRule();
void refreshMenu();
void showNotice(const String& title, const String& msg, uint16_t color, unsigned long ms);
void handleButton(ButtonEvent ev);
void startSelectedPayment();
void finishQRRequest();
void showNewQRSession(const String& qrId, const String& qrPayload, float amount, uint32_t duration);
void onRelayFinished();
void syncRuntime();

// Callbacks used by the admin API (LAN desktop app)
bool apiStartPayment(float amount, uint32_t duration, String& err);
void apiCancelPayment();
void apiServiceChanged(bool enabled);
bool apiRelayTest(uint32_t seconds, String& err);
void apiRelayOff();
void apiConfigChanged();

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n========================================================");
    Serial.printf(" FUNNHUB ARCADE PAYMENT SYSTEM - FIRMWARE %s\n", FIRMWARE_VERSION);
    Serial.println("========================================================");

    // 1. Storage (NVS)
    storage.begin();
    DeviceSettings& s = storage.getSettings();

    // 2. Relay (fail-safe OFF)
    relay.begin(s.relayPin, s.relayActiveHigh);

    // 3. Menu buttons
    buttons.begin();

    // 4. TFT display
    display.begin(s);

    // 5. Wi-Fi (hotspot + station)
    network.begin(s);

    // 6. Cloud payment client
    paymentClient.begin(s.backendUrl, s.deviceKey);

    // 7. Local network services
    discovery.begin(&s);

    AdminCallbacks callbacks;
    callbacks.startPayment = apiStartPayment;
    callbacks.cancelPayment = apiCancelPayment;
    callbacks.serviceChanged = apiServiceChanged;
    callbacks.relayTest = apiRelayTest;
    callbacks.relayOff = apiRelayOff;
    callbacks.configChanged = apiConfigChanged;
    adminApi.begin(&storage, &relay, &network, &runtime, callbacks);
    setupPortal.begin(&storage, &network);

    delay(1000);

    // 8. Ready state: show the menu
    relay.setState(STATE_READY);
    menuScreen = MENU_HOME;
    selectedRuleIndex = 0;
    refreshMenu();

    Serial.printf("[MAIN] Ready. Device '%s' | service %s | hotspot '%s'\n",
                  s.deviceName, s.serviceEnabled ? "ENABLED" : "STOPPED", s.apSSID);
}

void loop() {
    // 1. Background services
    network.update();
    adminApi.update();
    setupPortal.update();
    discovery.update();

    // Apply backend/card changes saved by the admin app without rebooting
    if (configDirty) {
        configDirty = false;
        DeviceSettings& cfg = storage.getSettings();
        paymentClient.setConfig(cfg.backendUrl, cfg.deviceKey);
        Serial.printf("[MAIN] Configuration applied (backend: %s)\n", cfg.backendUrl);
        if (relay.getState() == STATE_READY && menuScreen != MENU_NOTICE && menuScreen != MENU_LOADING) {
            normalizeSelectedRule();
            refreshMenu();
        }
    }

    // 2. Menu navigation (fully debounced multi-sample filter)
    handleButton(buttons.poll());

    // 2b. Background QR request: animate the loading bar and pick up the result
    if (qrRequestBusy) {
        unsigned long now = millis();
        if (now - lastLoadingFrame >= 50) {
            lastLoadingFrame = now;
            display.updateLoadingBar(now - qrRequestStart);
        }
        if (qrRequestDone) {
            qrRequestBusy = false;
            finishQRRequest();
        }
    }

    // 3. Relay timer (non-blocking)
    if (relay.update()) {
        onRelayFinished();
    }

    GatewayState currentState = relay.getState();

    // 4. Payment / relay state machine
    switch (currentState) {
        case STATE_READY:
            break;

        case STATE_QR_ACTIVE: {
            unsigned long now = millis();
            unsigned long elapsedSec = (now - qrStartTime) / 1000;

            if (elapsedSec >= QR_EXPIRY_SECONDS) {
                Serial.println("[MAIN] QR payment session expired.");
                relay.setState(STATE_ERROR);
                errorStartTime = millis();
                display.showPaymentFailedScreen("Payment Expired");
                resetPaymentSession();
                break;
            }

            // Countdown on the QR screen, once per second
            if (now - lastDisplayUpdate >= 1000) {
                lastDisplayUpdate = now;
                uint32_t remainingSec = QR_EXPIRY_SECONDS - elapsedSec;
                display.updateQRCountdown(remainingSec);
            }

            // Verify payment with the cloud backend
            if (now - lastPollTime >= STATUS_POLL_INTERVAL_MS) {
                lastPollTime = now;
                String status, paymentId, authToken, err;

                if (paymentClient.checkPaymentStatus(activeQrId, status, paymentId, authToken, err)) {
                    if (status == "captured") {
                        DeviceSettings& s = storage.getSettings();
                        Serial.printf("[MAIN] Payment CAPTURED! ID=%s. Relay for %us (mode2=%d)\n",
                                      paymentId.c_str(),
                                      s.mode2Enabled ? MODE2_RELAY_SECONDS : activeDuration,
                                      s.mode2Enabled ? 1 : 0);

                        // Anti-replay: reject a payment ID we already honoured
                        if (storage.isPaymentConsumed(paymentId.c_str())) {
                            Serial.println("[SECURITY ALERT] Duplicate payment token detected! Rejecting replay.");
                            relay.setState(STATE_ERROR);
                            errorStartTime = millis();
                            display.showPaymentFailedScreen("Duplicate Token");
                            resetPaymentSession();
                            break;
                        }

                        paymentClient.consumePayment(activeQrId, paymentId, authToken);

                        // 2nd mode: fixed 5-second relay regardless of the card's configured duration
                        uint32_t relayDuration = s.mode2Enabled ? MODE2_RELAY_SECONDS : activeDuration;
                        bool isMode2 = s.mode2Enabled;

                        // Record the real payment ID so replays are caught later (log the actual relay time)
                        storage.logTransaction(activeQrId.c_str(), paymentId.c_str(),
                                               activeAmount, relayDuration, true);

                        relayIsPayment = true;
                        mode2Active = isMode2;
                        relay.activate(relayDuration);
                        relay.setState(STATE_RELAY_ON);

                        if (isMode2) {
                            // "enjoy the game" only on the device
                            display.showMode2SuccessScreen(activeAmount);
                        } else {
                            display.showPaymentSuccessScreen(activeAmount, activeDuration, relay.getRemainingSeconds());
                        }
                        lastDisplayUpdate = millis();
                    } else if (status == "expired") {
                        relay.setState(STATE_ERROR);
                        errorStartTime = millis();
                        display.showPaymentFailedScreen("Session Expired");
                        resetPaymentSession();
                    }
                } else if (storage.getSettings().debugMode) {
                    Serial.printf("[DEBUG] Poll status note: %s\n", err.c_str());
                }
            }
            break;
        }

        case STATE_RELAY_ON: {
            if (millis() - lastDisplayUpdate >= 1000) {
                lastDisplayUpdate = millis();
                uint32_t rem = relay.getRemainingSeconds();
                if (mode2Active) {
                    // Mode 2: device-only "enjoy the game" countdown
                    display.updateMode2RelayStatus(rem);
                } else if (relayIsPayment) {
                    display.updatePaymentSuccessRelayStatus(rem);
                } else {
                    display.showNoticeScreen("RELAY TEST", "ON for " + String(rem) + " s", COLOR_CYAN);
                }
            }
            break;
        }

        case STATE_ERROR: {
            // Keep the failure screen for 3 seconds, then return to the menu
            if (millis() - errorStartTime >= 3000) {
                relay.setState(STATE_READY);
                menuScreen = MENU_HOME;
                refreshMenu();
            }
            break;
        }

        default:
            break;
    }

    // 5. Menu housekeeping while idle
    if (relay.getState() == STATE_READY) {
        if (menuScreen == MENU_NOTICE && millis() >= noticeUntil) {
            menuScreen = MENU_HOME;
            refreshMenu();
        } else if (menuScreen == MENU_HOME || menuScreen == MENU_INFO) {
            // Redraw only when the Wi-Fi state actually changes (no periodic full-screen repaint)
            bool curWifi = network.isConnectedToInternet();
            if (curWifi != lastWifiState) {
                refreshMenu();
            }
        }
    }

    syncRuntime();
    yield();
}

// ============================================================================
// Payment session
// ============================================================================

// Creates a QR session on the cloud backend and shows the QR on the TFT
bool triggerNewPayment(float amount, uint32_t duration, String& outError) {
    if (amount <= 0.0f) {
        outError = "Invalid amount";
        return false;
    }

    if (duration == 0) {
        duration = storage.getDurationForAmount(amount);
    }

    String qrId, qrPayload;
    bool ok = paymentClient.createQRSession(amount, duration, qrId, qrPayload, outError);

    if (!ok) {
        Serial.printf("[MAIN] Failed to create payment QR: %s\n", outError.c_str());
        relay.setState(STATE_ERROR);
        errorStartTime = millis();
        display.showPaymentFailedScreen(outError);
        return false;
    }

    showNewQRSession(qrId, qrPayload, amount, duration);
    return true;
}

// Switches to the QR screen and starts polling for this session
void showNewQRSession(const String& qrId, const String& qrPayload, float amount, uint32_t duration) {
    activeQrId = qrId;
    activeQrPayload = qrPayload;
    activeAmount = amount;
    activeDuration = duration;
    qrStartTime = millis();
    lastPollTime = millis();
    lastDisplayUpdate = millis();
    relayIsPayment = false;
    menuScreen = MENU_HOME;

    runtime.paymentActive = true;
    runtime.activeQrId = activeQrId;
    runtime.activeAmount = activeAmount;
    runtime.activeDuration = activeDuration;

    relay.setState(STATE_QR_ACTIVE);
    stateEnteredTime = millis();
    buttons.clear();
    display.showQRScreen(activeQrPayload, activeAmount, activeDuration, activeQrId, QR_EXPIRY_SECONDS);

    Serial.printf("[MAIN] QR shown. ID=%s, Amount=Rs. %.2f, Relay=%us\n",
                  activeQrId.c_str(), activeAmount, activeDuration);
}

// Runs on its own task: the blocking HTTPS call never freezes the loading bar
void qrRequestTask(void* param) {
    String id, payload, err;
    bool ok = paymentClient.createQRSession(qrReqAmount, qrReqDuration, id, payload, err);
    qrReqId = id;
    qrReqPayload = payload;
    qrReqError = err;
    qrReqOk = ok;
    __sync_synchronize();   // results must be visible before the flag is seen
    qrRequestDone = true;
    vTaskDelete(nullptr);
}

// Called from the main loop when the background request has finished
void finishQRRequest() {
    if (qrRequestCancelled) {
        qrRequestCancelled = false;
        Serial.println("[MAIN] QR response ignored because user cancelled.");
        return;
    }
    unsigned long waited = millis() - qrRequestStart;
    DeviceSettings& s = storage.getSettings();

    if (!s.serviceEnabled) {
        // Admin stopped the service while we were waiting: never show a QR
        relay.setState(STATE_ERROR);
        errorStartTime = millis();
        display.showPaymentFailedScreen("Service stopped");
        return;
    }
    if (!qrReqOk) {
        Serial.printf("[MAIN] Failed to create payment QR after %lu ms: %s\n",
                      waited, qrReqError.c_str());
        relay.setState(STATE_ERROR);
        errorStartTime = millis();
        display.showPaymentFailedScreen(qrReqError);
        return;
    }
    Serial.printf("[MAIN] QR ready in %lu ms\n", waited);
    showNewQRSession(qrReqId, qrReqPayload, qrReqAmount, qrReqDuration);
}

// Clears the payment session fields (does not touch the relay or the screen)
void resetPaymentSession() {
    paymentClient.endSession();
    activeQrId = "";
    activeQrPayload = "";
    activeAmount = 0.0f;
    activeDuration = 0;
    relayIsPayment = false;
    runtime.paymentActive = false;
    runtime.activeQrId = "";
    runtime.activeAmount = 0.0f;
    runtime.activeDuration = 0;
}

void cancelActivePayment() {
    String cancellingQrId = activeQrId;
    resetPaymentSession();
    relay.setState(STATE_READY);
    menuScreen = MENU_HOME;
    stateEnteredTime = millis();
    buttons.clear();
    refreshMenu();

    if (cancellingQrId.length() > 0) {
        // Fire-and-forget background task to close QR on Razorpay banking network
        char* idCopy = strdup(cancellingQrId.c_str());
        xTaskCreatePinnedToCore([](void* p) {
            char* id = (char*)p;
            String err;
            paymentClient.cancelQRSession(String(id), err);
            free(id);
            vTaskDelete(nullptr);
        }, "qr_cancel", 4096, idCopy, 1, nullptr, 0);
    }
}

// Called when the relay timer runs out
void onRelayFinished() {
    Serial.printf("[RELAY] Cycle finished (%s). Game Over.\n", relayIsPayment ? "payment" : "test");
    bool wasMode2 = mode2Active;
    relay.setState(STATE_READY);
    resetPaymentSession();
    mode2Active = false;
    if (wasMode2) {
        display.updateMode2RelayStatus(0);  // "ENJOY YOUR GAME"
    } else {
        display.updatePaymentSuccessRelayStatus(0);
    }
    delay(1200);
    menuScreen = MENU_HOME;
    refreshMenu();
}

// ============================================================================
// Menu / display
// ============================================================================

// Keep the selected card inside the configured range
void normalizeSelectedRule() {
    DeviceSettings& s = storage.getSettings();
    if (s.ruleCount <= 0) {
        selectedRuleIndex = 0;
        return;
    }
    if (selectedRuleIndex < 0) selectedRuleIndex = 0;
    if (selectedRuleIndex >= s.ruleCount) selectedRuleIndex = s.ruleCount - 1;
}

// Redraws the screen for the current menu page
void refreshMenu() {
    normalizeSelectedRule();
    DeviceSettings& s = storage.getSettings();
    bool connected = network.isConnectedToInternet();
    lastWifiState = connected;
    lastDisplayUpdate = millis();

    switch (menuScreen) {
        case MENU_HOME:
            display.showMenuHome(String(s.deviceName), s.serviceEnabled, connected,
                                 network.getStaIP(), s.ruleCount, s.rules, selectedRuleIndex);
            break;

        case MENU_CONFIRM: {
            float amount = 0.0f;
            uint32_t duration = 0;
            if (s.ruleCount > 0) {
                amount = s.rules[selectedRuleIndex].amount;
                duration = s.rules[selectedRuleIndex].durationSec;
            }
            display.showConfirmScreen(amount, duration);
            break;
        }

        case MENU_INFO:
            display.showInfoScreen(String(s.deviceName), String(FIRMWARE_VERSION), String(s.apSSID),
                                   connected, network.getStaIP(), s.serviceEnabled, relay.isRelayOn());
            break;

        case MENU_NOTICE:
        case MENU_LOADING:
            break; // drawn by showNotice() / showLoadingScreen()
    }
}

// Full-screen message that closes itself after `ms`
void showNotice(const String& title, const String& msg, uint16_t color, unsigned long ms) {
    menuScreen = MENU_NOTICE;
    noticeUntil = millis() + ms;
    display.showNoticeScreen(title, msg, color);
}

// Mirrors gateway state into the status object read by the admin API
void syncRuntime() {
    runtime.state = stateToString(relay.getState());
    runtime.relayOn = relay.isRelayOn();
    runtime.relayRemainingSec = relay.getRemainingSeconds();
}

// ============================================================================
// Buttons: ENTER select / pay, UP-DOWN move, RIGHT info, LEFT back / cancel
// ============================================================================
void handleButton(ButtonEvent ev) {
    if (ev == BTN_NONE) return;

    GatewayState st = relay.getState();
    Serial.printf("[BUTTON] %s (state %s)\n", buttonEventName(ev), stateToString(st));

    // While a QR is up, LEFT (Back) or ENTER (Select) cancels immediately
    if (st == STATE_QR_ACTIVE) {
        if (ev == BTN_LEFT || ev == BTN_ENTER) {
            Serial.println("[MAIN] Payment cancelled from QR screen by user.");
            cancelActivePayment();
        }
        return;
    }

    // While waiting for bank / QR generation (MENU_LOADING), allow user to cancel with LEFT
    if (menuScreen == MENU_LOADING) {
        if (ev == BTN_LEFT) {
            Serial.println("[MAIN] Bank connection cancelled by user.");
            qrRequestBusy = false;
            qrRequestCancelled = true;
            menuScreen = MENU_HOME;
            buttons.clear();
            refreshMenu();
        }
        return;
    }

    // Paying / relay running / error screen: buttons are locked
    if (st != STATE_READY) return;
    if (menuScreen == MENU_NOTICE) return;

    DeviceSettings& s = storage.getSettings();

    switch (menuScreen) {
        case MENU_HOME:
            if (ev == BTN_UP || ev == BTN_DOWN) {
                if (s.ruleCount > 1) {
                    if (ev == BTN_UP) {
                        selectedRuleIndex = (selectedRuleIndex - 1 + s.ruleCount) % s.ruleCount;
                    } else {
                        selectedRuleIndex = (selectedRuleIndex + 1) % s.ruleCount;
                    }
                    refreshMenu();
                }
            } else if (ev == BTN_ENTER) {
                if (!s.serviceEnabled) {
                    showNotice("SERVICE STOPPED", "Payments are disabled by admin", COLOR_RED, 2500);
                } else if (s.ruleCount == 0) {
                    showNotice("NO CARDS", "Add a payment card in the admin app", COLOR_ORANGE, 2500);
                } else {
                    menuScreen = MENU_CONFIRM;
                    refreshMenu();
                }
            } else if (ev == BTN_RIGHT) {
                menuScreen = MENU_INFO;
                refreshMenu();
            }
            break;

        case MENU_CONFIRM:
            if (ev == BTN_ENTER) {
                startSelectedPayment();
            } else if (ev == BTN_LEFT) {
                menuScreen = MENU_HOME;
                refreshMenu();
            }
            break;

        case MENU_INFO:
            if (ev == BTN_LEFT) {
                menuScreen = MENU_HOME;
                refreshMenu();
            }
            break;

        case MENU_NOTICE:
            break;
    }
}

void startSelectedPayment() {
    DeviceSettings& s = storage.getSettings();
    if (!s.serviceEnabled) {
        showNotice("SERVICE STOPPED", "Payments are disabled by admin", COLOR_RED, 2500);
        return;
    }
    if (s.ruleCount == 0) {
        showNotice("NO CARDS", "Add a payment card in the admin app", COLOR_ORANGE, 2500);
        return;
    }

    normalizeSelectedRule();
    float amount = s.rules[selectedRuleIndex].amount;
    uint32_t duration = s.rules[selectedRuleIndex].durationSec;

    if (qrRequestBusy) return;   // one request at a time

    Serial.printf("[GAME] Selected Rs. %.2f for %u s play time\n", amount, duration);

    // Show the loading bar at once, then ask the cloud in the background
    qrReqAmount = amount;
    qrReqDuration = duration;
    qrRequestDone = false;
    qrRequestBusy = true;
    qrRequestStart = millis();
    lastLoadingFrame = 0;
    menuScreen = MENU_LOADING;
    stateEnteredTime = millis();
    buttons.clear();
    display.showLoadingScreen(amount);

    BaseType_t created = xTaskCreatePinnedToCore(qrRequestTask, "qr_request", 12288,
                                                 nullptr, 1, nullptr, 0);
    if (created != pdPASS) {
        qrRequestBusy = false;
        qrReqOk = false;
        qrReqError = "Could not start request";
        finishQRRequest();
    }
}

// ============================================================================
// Admin API callbacks (desktop app)
// ============================================================================
bool apiStartPayment(float amount, uint32_t duration, String& err) {
    if (!storage.getSettings().serviceEnabled) {
        err = "Service is stopped";
        return false;
    }
    if (relay.getState() != STATE_READY) {
        err = "Device is busy";
        return false;
    }
    return triggerNewPayment(amount, duration, err);
}

void apiCancelPayment() {
    if (relay.getState() == STATE_QR_ACTIVE) {
        cancelActivePayment();
    }
}

void apiServiceChanged(bool enabled) {
    if (!enabled) {
        // Stop everything: cancel a pending QR and cut the relay immediately
        if (relay.getState() == STATE_QR_ACTIVE) {
            resetPaymentSession();
        }
        if (relay.getState() == STATE_RELAY_ON) {
            relay.emergencyStop();
            resetPaymentSession();
        }
        relay.setState(STATE_READY);
        menuScreen = MENU_HOME;
        showNotice("SERVICE STOPPED", "Stopped by admin", COLOR_RED, 3000);
    } else {
        showNotice("SERVICE ENABLED", "Ready for payments", COLOR_GREEN, 2000);
    }
    Serial.printf("[MAIN] Service %s\n", enabled ? "enabled" : "stopped");
}

bool apiRelayTest(uint32_t seconds, String& err) {
    if (relay.getState() != STATE_READY) {
        err = "Relay busy";
        return false;
    }
    relayIsPayment = false;
    menuScreen = MENU_HOME;
    lastDisplayUpdate = millis();
    relay.activate(seconds);
    display.showNoticeScreen("RELAY TEST", "ON for " + String(seconds) + " s", COLOR_CYAN);
    return true;
}

void apiRelayOff() {
    if (relay.getState() == STATE_RELAY_ON) {
        relay.emergencyStop();
        resetPaymentSession();
        menuScreen = MENU_HOME;
        refreshMenu();
    }
}

void apiConfigChanged() {
    configDirty = true;
}
