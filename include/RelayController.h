#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <Arduino.h>
#include "Config.h"

// Phase of the current relay sequence
enum RelayPhase {
    PHASE_IDLE,     // nothing running (relay off)
    PHASE_ON,       // relay energized
    PHASE_OFF,      // relay de-energized between pulses
    PHASE_WAIT,     // waiting before returning home (relay off, e.g. "best of luck" message)
    PHASE_DONE      // sequence finished, waiting for main.cpp to observe
};

class RelayController {
private:
    int relayPin = DEFAULT_RELAY_PIN;
    bool activeHigh = DEFAULT_RELAY_ACTIVE_HIGH;
    bool relayState = false; // true = ON, false = OFF

    uint32_t phaseStartTime = 0;
    uint32_t phaseDurationMs = 0;
    RelayPhase phase = PHASE_IDLE;
    GatewayState currentState = STATE_STARTUP;

    // Pulse sequence parameters
    uint32_t pulseOnMs = 0;      // ON time per pulse (ms)
    uint32_t pulseOffMs = 0;     // OFF time between pulses (ms)
    uint32_t pulseTotal = 0;     // total pulses requested
    uint32_t pulseIndex = 0;     // pulses completed so far (1-based)
    uint32_t waitMs = 0;         // wait time after the final OFF (message mode)
    bool messageMode = false;    // true = after final OFF, wait then done

public:
    RelayController() {}

    void begin(int pin, bool isActiveHigh) {
        relayPin = pin;
        activeHigh = isActiveHigh;

        pinMode(relayPin, OUTPUT);
        // Guarantee relay is safely OFF on boot
        forceOff();
        currentState = STATE_READY;
        phase = PHASE_IDLE;

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

    // Normal timer: relay ON for durationSeconds once, then sequence done.
    void activateTimer(uint32_t durationSeconds) {
        if (durationSeconds == 0) return;
        stopSequence();
        forceOn();
        phase = PHASE_ON;
        phaseStartTime = millis();
        phaseDurationMs = durationSeconds * 1000UL;
        currentState = STATE_RELAY_ON;
        pulseTotal = 0;
        pulseIndex = 0;
        messageMode = false;
        waitMs = 0;

        Serial.printf("[RELAY] Timer activated for %u seconds (Pin %d = %s)\n",
                      durationSeconds, relayPin, activeHigh ? "HIGH" : "LOW");
    }

    // Single pulse then message + wait:
    // relay ON for onSec, OFF, then wait waitSec before the sequence is done.
    void activatePulseMessage(uint32_t onSec, uint32_t waitSec) {
        if (onSec == 0) return;
        stopSequence();
        forceOn();
        phase = PHASE_ON;
        phaseStartTime = millis();
        phaseDurationMs = onSec * 1000UL;
        currentState = STATE_RELAY_ON;
        pulseTotal = 1;
        pulseIndex = 0;
        messageMode = true;
        waitMs = waitSec * 1000UL;
        pulseOnMs = onSec * 1000UL;
        pulseOffMs = 0;

        Serial.printf("[RELAY] Pulse message activated: ON %us, wait %us before home\n",
                      onSec, waitSec);
    }

    // Repeated pulses: ON onSec / OFF offSec, repeated count times, then done.
    void activatePulseCount(uint32_t count, uint32_t onSec, uint32_t offSec) {
        if (count == 0 || onSec == 0) return;
        stopSequence();
        forceOn();
        phase = PHASE_ON;
        phaseStartTime = millis();
        phaseDurationMs = onSec * 1000UL;
        currentState = STATE_RELAY_ON;
        pulseTotal = count;
        pulseIndex = 0;
        messageMode = false;
        waitMs = 0;
        pulseOnMs = onSec * 1000UL;
        pulseOffMs = offSec * 1000UL;

        Serial.printf("[RELAY] Pulse count activated: %u x (ON %us / OFF %us)\n",
                      count, onSec, offSec);
    }

    // Stop any running sequence and turn the relay off immediately.
    void stopSequence() {
        forceOff();
        phase = PHASE_IDLE;
        phaseStartTime = 0;
        phaseDurationMs = 0;
        pulseTotal = 0;
        pulseIndex = 0;
        waitMs = 0;
        messageMode = false;
        pulseOnMs = 0;
        pulseOffMs = 0;
    }

    void deactivate() {
        forceOff();
        currentState = STATE_COMPLETE;
        phase = PHASE_IDLE;
        Serial.printf("[RELAY] Deactivated automatically (Pin %d = %s)\n",
                      relayPin, activeHigh ? "LOW" : "HIGH");
    }

    void emergencyStop() {
        forceOff();
        currentState = STATE_READY;
        phase = PHASE_IDLE;
        pulseTotal = 0;
        pulseIndex = 0;
        messageMode = false;
        waitMs = 0;
        Serial.println("[RELAY] Emergency stop triggered!");
    }

    // Called in main loop. Non-blocking state update.
    // Returns true when the whole sequence has finished.
    bool update() {
        if (currentState != STATE_RELAY_ON) return false;

        uint32_t now = millis();
        uint32_t elapsed = now - phaseStartTime;

        if (phase == PHASE_ON) {
            if (elapsed >= phaseDurationMs) {
                forceOff();
                pulseIndex++; // one ON pulse completed
                if (messageMode) {
                    // single pulse: go straight to the wait-then-home phase
                    phase = PHASE_WAIT;
                    phaseStartTime = now;
                    phaseDurationMs = waitMs;
                    Serial.println("[RELAY] Pulse ON done -> waiting before home");
                } else if (pulseIndex < pulseTotal) {
                    // more pulses: OFF then ON again
                    phase = PHASE_OFF;
                    phaseStartTime = now;
                    phaseDurationMs = pulseOffMs;
                    Serial.printf("[RELAY] Pulse %u/%u ON done -> OFF\n", pulseIndex, pulseTotal);
                } else {
                    // all pulses done
                    phase = PHASE_DONE;
                    currentState = STATE_READY;
                    Serial.println("[RELAY] All pulses done");
                    return true;
                }
            }
        } else if (phase == PHASE_OFF) {
            if (elapsed >= phaseDurationMs) {
                forceOn();
                phase = PHASE_ON;
                phaseStartTime = now;
                phaseDurationMs = pulseOnMs;
                Serial.printf("[RELAY] OFF done -> pulse %u/%u ON\n", pulseIndex + 1, pulseTotal);
            }
        } else if (phase == PHASE_WAIT) {
            if (elapsed >= phaseDurationMs) {
                phase = PHASE_DONE;
                currentState = STATE_READY;
                Serial.println("[RELAY] Wait done -> home");
                return true;
            }
        }
        return false;
    }

    uint32_t getRemainingSeconds() const {
        if (currentState != STATE_RELAY_ON) return 0;
        uint32_t elapsed = millis() - phaseStartTime;
        if (elapsed >= phaseDurationMs) return 0;
        return (phaseDurationMs - elapsed + 999) / 1000UL;
    }

    uint32_t getTotalDurationSeconds() const {
        return phaseDurationMs / 1000UL;
    }

    bool isRelayOn() const {
        return relayState;
    }

    // True while the relay is energized in the current phase
    bool isRelayOnPhase() const {
        return phase == PHASE_ON;
    }

    // True while waiting before home (message mode)
    bool isWaiting() const {
        return phase == PHASE_WAIT;
    }

    GatewayState getState() const {
        return currentState;
    }

    void setState(GatewayState state) {
        currentState = state;
        if (state != STATE_RELAY_ON && relayState) {
            forceOff();
            phase = PHASE_IDLE;
            pulseTotal = 0;
            pulseIndex = 0;
            messageMode = false;
            waitMs = 0;
        }
    }

    uint32_t getPulseIndex() const { return pulseIndex; }
    uint32_t getPulseTotal() const { return pulseTotal; }
};

#endif // RELAY_CONTROLLER_H
