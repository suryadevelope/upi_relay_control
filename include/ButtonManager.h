#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
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

// Industrial-grade FreeRTOS background button scanner with consecutive digital filtering.
// Runs independently at 5ms intervals (Priority 2):
// - Never blocked by loop() or network calls.
// - 4 consecutive LOW samples (20 ms hold) = Instant Valid Press.
// - 3 consecutive HIGH samples (15 ms release) = Valid Release.
// - Rejects all sub-millisecond electrical noise spikes, RF EMI, and contact chatter.
// - Fast human taps (80-150 ms) register immediately with zero perceptual latency.
class ButtonManager {
private:
    static const int COUNT = 5;
    const int pins[COUNT] = { PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_ENTER, PIN_BTN_LEFT, PIN_BTN_RIGHT };
    const ButtonEvent events[COUNT] = { BTN_UP, BTN_DOWN, BTN_ENTER, BTN_LEFT, BTN_RIGHT };

    uint8_t lowCount[COUNT];
    uint8_t highCount[COUNT];
    bool isPressed[COUNT];
    unsigned long lastEventTime[COUNT];

    QueueHandle_t eventQueue = nullptr;
    TaskHandle_t taskHandle = nullptr;

    static const uint8_t SAMPLES_FOR_PRESS = 4;      // 4 * 5ms = 20ms hold required
    static const uint8_t SAMPLES_FOR_RELEASE = 3;    // 3 * 5ms = 15ms release required
    static const unsigned long EVENT_COOLDOWN_MS = 120; // 120ms debounce cooldown per button

    static void taskTrampoline(void* arg) {
        static_cast<ButtonManager*>(arg)->taskLoop();
    }

    void taskLoop() {
        const TickType_t xDelay = pdMS_TO_TICKS(5); // exactly 5ms periodic scan
        while (true) {
            unsigned long now = millis();

            for (int i = 0; i < COUNT; i++) {
                int reading = digitalRead(pins[i]);

                if (reading == LOW) {
                    lowCount[i]++;
                    highCount[i] = 0;

                    if (lowCount[i] >= SAMPLES_FOR_PRESS) {
                        lowCount[i] = SAMPLES_FOR_PRESS; // saturate counter

                        // Only fire if button was not already pressed AND cooldown has elapsed
                        if (!isPressed[i] && (now - lastEventTime[i] >= EVENT_COOLDOWN_MS)) {
                            isPressed[i] = true;
                            lastEventTime[i] = now;
                            ButtonEvent ev = events[i];
                            if (eventQueue) {
                                xQueueSend(eventQueue, &ev, 0);
                            }
                        }
                    }
                } else {
                    highCount[i]++;
                    lowCount[i] = 0; // immediate reset on high: sub-millisecond noise cannot accumulate

                    if (highCount[i] >= SAMPLES_FOR_RELEASE) {
                        highCount[i] = SAMPLES_FOR_RELEASE;
                        isPressed[i] = false;
                    }
                }
            }

            vTaskDelay(xDelay);
        }
    }

public:
    void begin() {
        Serial.printf("[BUTTONS] ENTER=%d DOWN=%d UP=%d RIGHT=%d LEFT=%d (active LOW)\n",
                      PIN_BTN_ENTER, PIN_BTN_DOWN, PIN_BTN_UP, PIN_BTN_RIGHT, PIN_BTN_LEFT);

        if (!eventQueue) {
            eventQueue = xQueueCreate(16, sizeof(ButtonEvent));
        }

        for (int i = 0; i < COUNT; i++) {
            pinMode(pins[i], INPUT_PULLUP);
            lowCount[i] = 0;
            highCount[i] = SAMPLES_FOR_RELEASE;
            isPressed[i] = false;
            lastEventTime[i] = 0;

            int initial = digitalRead(pins[i]);
            Serial.printf("[BUTTONS] %-5s GPIO%-2d idle=%s\n", buttonEventName(events[i]),
                          pins[i], initial == LOW ? "LOW (check wiring!)" : "HIGH (ok)");
        }

        if (!taskHandle) {
            // Priority 2: preempts Arduino loopTask (priority 1), ensuring zero lost clicks
            xTaskCreatePinnedToCore(taskTrampoline, "btn_scan", 2048, this, 2, &taskHandle, 1);
        }
    }

    // Call from loop(). Non-blocking, returns next debounced event or BTN_NONE.
    ButtonEvent poll() {
        if (!eventQueue) return BTN_NONE;
        ButtonEvent ev = BTN_NONE;
        if (xQueueReceive(eventQueue, &ev, 0) == pdTRUE) {
            return ev;
        }
        return BTN_NONE;
    }

    // Flushes pending button events (useful during screen / state transitions)
    void clear() {
        if (eventQueue) {
            xQueueReset(eventQueue);
        }
    }

    bool isPressedStable(ButtonEvent e) const {
        for (int i = 0; i < COUNT; i++) {
            if (events[i] == e) return isPressed[i];
        }
        return false;
    }
};

#endif // BUTTON_MANAGER_H