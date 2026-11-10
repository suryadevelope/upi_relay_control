#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "Config.h"

// Menu navigation events produced by the 5 physical buttons
enum ButtonEvent {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_ENTER,
    BTN_LEFT,
    BTN_RIGHT
};

inline const char* buttonEventName(ButtonEvent e) {
    switch (e) {
        case BTN_UP:    return "UP";
        case BTN_DOWN:  return "DOWN";
        case BTN_ENTER: return "ENTER";
        case BTN_LEFT:  return "LEFT";
        case BTN_RIGHT: return "RIGHT";
        default:        return "NONE";
    }
}

// Debounced, edge-triggered reader for the 5 active-LOW buttons.
// Each physical press produces exactly one event on the falling edge.
class ButtonManager {
private:
    static const int COUNT = 5;
    const int pins[COUNT] = { PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_ENTER, PIN_BTN_LEFT, PIN_BTN_RIGHT };
    const ButtonEvent events[COUNT] = { BTN_UP, BTN_DOWN, BTN_ENTER, BTN_LEFT, BTN_RIGHT };

    int lastReading[COUNT];
    bool stablePressed[COUNT];
    unsigned long lastChange[COUNT];

    // Presses detected in the same scan are queued so none is lost
    ButtonEvent queue[COUNT];
    int queued = 0;

    static const unsigned long DEBOUNCE_MS = 35;

public:
    void begin() {
        Serial.printf("[BUTTONS] ENTER=%d DOWN=%d UP=%d RIGHT=%d LEFT=%d (active LOW)\n",
                      PIN_BTN_ENTER, PIN_BTN_DOWN, PIN_BTN_UP, PIN_BTN_RIGHT, PIN_BTN_LEFT);
        for (int i = 0; i < COUNT; i++) {
            pinMode(pins[i], INPUT_PULLUP);
            lastReading[i] = digitalRead(pins[i]);   // real level, so no false edge at boot
            stablePressed[i] = false;
            lastChange[i] = 0;
            // Idle must read HIGH. LOW at boot with nothing pressed = wired to GND or shorted.
            Serial.printf("[BUTTONS] %-5s GPIO%-2d idle=%s\n", buttonEventName(events[i]),
                          pins[i], lastReading[i] == LOW ? "LOW (check wiring!)" : "HIGH (ok)");
        }
    }

    // Call every loop iteration. Returns one queued press per call, or BTN_NONE.
    ButtonEvent poll() {
        unsigned long now = millis();

        for (int i = 0; i < COUNT; i++) {
            int reading = digitalRead(pins[i]);
            if (reading != lastReading[i]) {
                lastReading[i] = reading;
                lastChange[i] = now;
                Serial.printf("[BTN-RAW] %-5s GPIO%-2d -> %s\n", buttonEventName(events[i]),
                              pins[i], reading == LOW ? "LOW" : "HIGH");
                continue;
            }
            if (now - lastChange[i] < DEBOUNCE_MS) continue;

            bool pressed = (reading == LOW);
            if (pressed && !stablePressed[i]) {
                stablePressed[i] = true;
                if (queued < COUNT) queue[queued++] = events[i];
            } else if (!pressed && stablePressed[i]) {
                stablePressed[i] = false;
            }
        }

        if (queued == 0) return BTN_NONE;
        ButtonEvent next = queue[0];
        for (int i = 1; i < queued; i++) queue[i - 1] = queue[i];
        queued--;
        return next;
    }
};

#endif // BUTTON_MANAGER_H
