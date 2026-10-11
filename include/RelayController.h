#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <Arduino.h>
#include "Config.h"

class RelayController {
private:
    int relayPin = DEFAULT_RELAY_PIN;
    bool activeHigh = DEFAULT_RELAY_ACTIVE_HIGH;
    bool relayState = false; // true = ON, false = OFF

    uint32_t activationStartTime = 0;
    uint32_t totalDurationMs = 0;
    GatewayState currentState = STATE_STARTUP;

public:
    RelayController() {}

    void begin(int pin, bool isActiveHigh) {
        relayPin = pin;
        activeHigh = isActiveHigh;

        pinMode(relayPin, OUTPUT);
        // Guarantee relay is safely OFF on boot
        forceOff();
        currentState = STATE_READY;

        Serial.printf("[RELAY] Initialized on GPIO %d (Active %s, Initial state: OFF)\n",
                      relayPin, activeHigh ? "HIGH" : "LOW");
    }

    void forceOff() {
        relayState = false;
        digitalWrite(relayPin, activeHigh ? LOW : HIGH);
    }

    void forceOn() {
        relayState = true;
        digitalWrite(relayPin, activeHigh ? HIGH : LOW);
    }

    bool activate(uint32_t durationSeconds) {
        if (durationSeconds == 0) return false;

        forceOn();
        activationStartTime = millis();
        totalDurationMs = durationSeconds * 1000UL;
        currentState = STATE_RELAY_ON;

        Serial.printf("[RELAY] Activated for %u seconds (Pin %d = %s)\n",
                      durationSeconds, relayPin, activeHigh ? "HIGH" : "LOW");
        return true;
    }

    void deactivate() {
        forceOff();
        currentState = STATE_COMPLETE;
        Serial.printf("[RELAY] Deactivated automatically (Pin %d = %s)\n",
                      relayPin, activeHigh ? "LOW" : "HIGH");
    }

    void emergencyStop() {
        forceOff();
        currentState = STATE_READY;
        Serial.println("[RELAY] Emergency stop triggered!");
    }

    // Called in main loop. Non-blocking state update.
    bool update() {
        if (currentState == STATE_RELAY_ON) {
            uint32_t elapsed = millis() - activationStartTime;
            if (elapsed >= totalDurationMs) {
                deactivate();
                return true; // Just finished!
            }
        }
        return false;
    }

    uint32_t getRemainingSeconds() const {
        if (currentState != STATE_RELAY_ON) return 0;
        uint32_t elapsed = millis() - activationStartTime;
        if (elapsed >= totalDurationMs) return 0;
        return (totalDurationMs - elapsed + 999) / 1000UL;
    }

    uint32_t getTotalDurationSeconds() const {
        return totalDurationMs / 1000UL;
    }

    bool isRelayOn() const {
        return relayState;
    }

    GatewayState getState() const {
        return currentState;
    }

    void setState(GatewayState state) {
        currentState = state;
        if (state != STATE_RELAY_ON && relayState) {
            forceOff();
        }
    }
};

#endif // RELAY_CONTROLLER_H
