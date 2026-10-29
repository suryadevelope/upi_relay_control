#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ILI9341.h>
#include "Config.h"
#include "QRCodeRenderer.h"

// Color 565 Definitions
#define COLOR_BG          0x0841  // Deep slate background
#define COLOR_PANEL       0x18E3  // Dark slate card background
#define COLOR_WHITE       0xFFFF
#define COLOR_BLACK       0x0000
#define COLOR_GREEN       0x07E0  // Bright green
#define COLOR_EMERALD     0x2E6B  // Emerald green
#define COLOR_RED         0xF800  // Bright red
#define COLOR_CRIMSON     0xD8A7  // Deep red
#define COLOR_CYAN        0x07FF  // Cyan
#define COLOR_YELLOW      0xFFE0  // Yellow
#define COLOR_ORANGE      0xFD20  // Orange
#define COLOR_GRAY        0x7BEF  // Light gray
#define COLOR_DARK_GRAY   0x39E7  // Dark gray

class DisplayManager {
private:
    Adafruit_ST7789* st7789Display = nullptr;
    Adafruit_ILI9341* ili9341Display = nullptr;
    Adafruit_GFX* activeDisplay = nullptr;

    int screenWidth = 320;
    int screenHeight = 240;
    int displayType = DISPLAY_ILI9341;
    int rotation = 1;
    int blPin = -1;

public:
    DisplayManager() {}

    ~DisplayManager() {
        if (st7789Display) delete st7789Display;
        if (ili9341Display) delete ili9341Display;
    }

    void begin(const DeviceSettings& settings) {
        displayType = settings.displayType;
        rotation = settings.tftRotation;
        blPin = settings.tftBL;

        if (blPin >= 0) {
            pinMode(blPin, OUTPUT);
            digitalWrite(blPin, HIGH); // Turn backlight on
        }

        // Initialize ESP32 VSPI bus pins explicitly: SCK=18, MISO=19, MOSI=23, CS=settings.tftCS
        SPI.begin(DEFAULT_TFT_SCLK, DEFAULT_TFT_MISO, DEFAULT_TFT_MOSI, settings.tftCS);

        if (displayType == DISPLAY_ST7789) {
            Serial.printf("[DISPLAY] Initializing ST7789 (CS=%d, DC=%d, RST=%d)...\n",
                          settings.tftCS, settings.tftDC, settings.tftRST);
            st7789Display = new Adafruit_ST7789(settings.tftCS, settings.tftDC, settings.tftRST);
            st7789Display->init(240, 320);
            st7789Display->setRotation(rotation);
            activeDisplay = st7789Display;
        } else {
            // Default: 2.8 inch SPI Touch Screen Module TFT 240x320 (ILI9341)
            Serial.printf("[DISPLAY] Initializing ILI9341 2.8\" 240x320 (CS=%d, DC=%d, RST=%d)...\n",
                          settings.tftCS, settings.tftDC, settings.tftRST);
            ili9341Display = new Adafruit_ILI9341(settings.tftCS, settings.tftDC, settings.tftRST);
            ili9341Display->begin();
            ili9341Display->setRotation(rotation);
            activeDisplay = ili9341Display;
        }

        if (activeDisplay) {
            screenWidth = activeDisplay->width();
            screenHeight = activeDisplay->height();
            activeDisplay->fillScreen(COLOR_BLACK);
            Serial.printf("[DISPLAY] TFT Display Ready: %dx%d (Rotation: %d)\n", screenWidth, screenHeight, rotation);
        }
    }

    // =========================================================================
    // SCREEN 1: HOME PAGE
    // =========================================================================
    void showHomeScreen(const String& apSSID, const String& staIP, bool staConnected, 
                        int ruleCount, const PaymentRule* rules) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        if (screenWidth >= 300) {
            // Landscape (320x240) Home Page
            // Top Header Bar
            activeDisplay->fillRect(0, 0, screenWidth, 38, COLOR_PANEL);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(15, 10);
            activeDisplay->print("PAYMENT GATEWAY");

            // Wi-Fi Status Dot & Label
            if (staConnected) {
                activeDisplay->fillCircle(screenWidth - 20, 18, 6, COLOR_GREEN);
            } else {
                activeDisplay->fillCircle(screenWidth - 20, 18, 6, COLOR_ORANGE);
            }

            // Available Price Rule Cards
            int maxShow = ruleCount > 3 ? 3 : (ruleCount > 0 ? ruleCount : 1);
            int cardW = (screenWidth - 28 - ((maxShow - 1) * 8)) / maxShow;
            int startY = 48;

            for (int i = 0; i < maxShow && i < ruleCount; i++) {
                int cardX = 14 + i * (cardW + 8);
                activeDisplay->fillRoundRect(cardX, startY, cardW, 85, 6, COLOR_PANEL);
                activeDisplay->drawRoundRect(cardX, startY, cardW, 85, 6, COLOR_CYAN);

                // Price Amount
                activeDisplay->setTextSize(3);
                activeDisplay->setTextColor(COLOR_GREEN);
                activeDisplay->setCursor(cardX + 12, startY + 16);
                activeDisplay->printf("Rs.%d", (int)rules[i].amount);

                // Load Duration
                activeDisplay->setTextSize(1);
                activeDisplay->setTextColor(COLOR_YELLOW);
                activeDisplay->setCursor(cardX + 14, startY + 54);
                activeDisplay->printf("Power: %u Sec", rules[i].durationSec);
            }

            // Prominent "Press D25 to Start" Action Box
            activeDisplay->fillRoundRect(14, 142, screenWidth - 28, 48, 8, 0x0320); // Dark emerald
            activeDisplay->drawRoundRect(14, 142, screenWidth - 28, 48, 8, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(28, 157);
            activeDisplay->print("PRESS D25 TO PAY");

            // Subtitle inside action button
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->setCursor(screenWidth - 105, 162);
            activeDisplay->print("START >");

            // Bottom Footer with AP & Network Info
            activeDisplay->fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_GRAY);
            activeDisplay->setCursor(14, screenHeight - 15);
            if (staConnected) {
                activeDisplay->printf("WiFi: %s | Web: %s", staIP.c_str(), apSSID.c_str());
            } else {
                activeDisplay->printf("Admin AP: %s (192.168.4.1)", apSSID.c_str());
            }
        } else {
            // Portrait (240x320) Home Page
            activeDisplay->fillRect(0, 0, screenWidth, 38, COLOR_PANEL);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(12, 10);
            activeDisplay->print("PAY GATEWAY");

            if (staConnected) {
                activeDisplay->fillCircle(screenWidth - 15, 18, 5, COLOR_GREEN);
            } else {
                activeDisplay->fillCircle(screenWidth - 15, 18, 5, COLOR_ORANGE);
            }

            // Main rule card
            if (ruleCount > 0) {
                activeDisplay->fillRoundRect(15, 55, screenWidth - 30, 110, 8, COLOR_PANEL);
                activeDisplay->drawRoundRect(15, 55, screenWidth - 30, 110, 8, COLOR_CYAN);

                activeDisplay->setTextSize(4);
                activeDisplay->setTextColor(COLOR_GREEN);
                activeDisplay->setCursor(35, 75);
                activeDisplay->printf("Rs.%d", (int)rules[0].amount);

                activeDisplay->setTextSize(2);
                activeDisplay->setTextColor(COLOR_YELLOW);
                activeDisplay->setCursor(35, 125);
                activeDisplay->printf("%u Seconds", rules[0].durationSec);
            }

            // Big Start Button Banner
            activeDisplay->fillRoundRect(15, 185, screenWidth - 30, 60, 8, 0x0320);
            activeDisplay->drawRoundRect(15, 185, screenWidth - 30, 60, 8, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(25, 205);
            activeDisplay->print("PRESS D25 TO PAY");

            activeDisplay->fillRect(0, screenHeight - 25, screenWidth, 25, COLOR_PANEL);
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_GRAY);
            activeDisplay->setCursor(10, screenHeight - 16);
            activeDisplay->print("Ready for payment...");
        }
    }

    // Alias for compatibility
    void showReadyScreen(const String& apSSID, const String& staIP, bool staConnected, 
                         int ruleCount, const PaymentRule* rules) {
        showHomeScreen(apSSID, staIP, staConnected, ruleCount, rules);
    }

    // =========================================================================
    // SCREEN 2: QR CODE & COUNTDOWN TIMER
    // =========================================================================
    void showQRScreen(const String& qrPayload, float amount, uint32_t durationSec, 
                      const String& qrId, uint32_t remainingSec) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        if (screenWidth >= 300) {
            // Landscape Layout (320x240)
            // 1. Header Bar with Amount
            activeDisplay->fillRect(0, 0, screenWidth, 36, COLOR_PANEL);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->setCursor(12, 10);
            activeDisplay->printf("PAY Rs. %.2f", amount);

            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_YELLOW);
            activeDisplay->setCursor(screenWidth - 105, 14);
            activeDisplay->printf("%u Sec Load", durationSec);

            // 2. High-contrast QR Code Card (Left Side)
            int cardSize = 172;
            int cardX = 8;
            int cardY = 42;
            QRCodeRenderer::renderToDisplay(activeDisplay, qrPayload, cardX, cardY, cardSize, COLOR_BLACK, COLOR_WHITE);

            // 3. Right Side Panel: App Hints and Countdown Timer
            int infoX = 186;
            int panelW = screenWidth - infoX - 8;
            activeDisplay->fillRoundRect(infoX, 42, panelW, 172, 6, COLOR_PANEL);
            activeDisplay->drawRoundRect(infoX, 42, panelW, 172, 6, COLOR_CYAN);

            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_CYAN);
            activeDisplay->setCursor(infoX + 10, 52);
            activeDisplay->print("SCAN & PAY VIA:");

            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(infoX + 10, 68);
            activeDisplay->print(" Google Pay");
            activeDisplay->setCursor(infoX + 10, 82);
            activeDisplay->print(" PhonePe / Paytm");
            activeDisplay->setCursor(infoX + 10, 96);
            activeDisplay->print(" Any UPI App");

            activeDisplay->drawFastHLine(infoX + 8, 114, panelW - 16, COLOR_DARK_GRAY);

            activeDisplay->setTextColor(COLOR_YELLOW);
            activeDisplay->setCursor(infoX + 10, 122);
            activeDisplay->print("Time Left:");

            // Countdown digits
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(infoX + 10, 138);
            activeDisplay->printf("%02d:%02d", remainingSec / 60, remainingSec % 60);

            activeDisplay->drawFastHLine(infoX + 8, 162, panelW - 16, COLOR_DARK_GRAY);

            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->setCursor(infoX + 10, 170);
            activeDisplay->print("Razorpay");
            activeDisplay->setTextColor(COLOR_GRAY);
            activeDisplay->setCursor(infoX + 10, 184);
            activeDisplay->print("Verified QR");

            // 4. Bottom Footer Status Line
            activeDisplay->fillRect(0, screenHeight - 18, screenWidth, 18, COLOR_PANEL);
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_CYAN);
            activeDisplay->setCursor(10, screenHeight - 13);
            activeDisplay->print("Waiting for payment...");
        } else {
            // Portrait Layout (240x320)
            activeDisplay->fillRect(0, 0, screenWidth, 36, COLOR_PANEL);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->setCursor(10, 10);
            activeDisplay->printf("PAY Rs. %.2f", amount);

            int cardSize = 196;
            int cardX = (screenWidth - cardSize) / 2;
            int cardY = 42;
            QRCodeRenderer::renderToDisplay(activeDisplay, qrPayload, cardX, cardY, cardSize, COLOR_BLACK, COLOR_WHITE);

            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_CYAN);
            activeDisplay->setCursor(15, 248);
            activeDisplay->print("Scan via GPay, PhonePe, Paytm, UPI");

            activeDisplay->setTextColor(COLOR_YELLOW);
            activeDisplay->setCursor(15, 268);
            activeDisplay->print("Expires in: ");
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->printf("%02d:%02d", remainingSec / 60, remainingSec % 60);

            activeDisplay->fillRect(0, screenHeight - 18, screenWidth, 18, COLOR_PANEL);
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_GRAY);
            activeDisplay->setCursor(10, screenHeight - 13);
            activeDisplay->print("Awaiting payment verification...");
        }
    }

    // Update ONLY the timer digits every second (zero screen redraw, zero flicker)
    void updateQRCountdown(uint32_t remainingSec) {
        if (!activeDisplay) return;

        if (screenWidth >= 300) {
            int infoX = 186;
            int timerX = infoX + 10;
            int timerY = 138;
            int timerW = 75;
            int timerH = 18;

            activeDisplay->fillRect(timerX, timerY, timerW, timerH, COLOR_PANEL);
            if (remainingSec <= 30) {
                activeDisplay->setTextColor(COLOR_RED);
            } else if (remainingSec <= 60) {
                activeDisplay->setTextColor(COLOR_ORANGE);
            } else {
                activeDisplay->setTextColor(COLOR_WHITE);
            }
            activeDisplay->setTextSize(2);
            activeDisplay->setCursor(timerX, timerY);
            activeDisplay->printf("%02d:%02d", remainingSec / 60, remainingSec % 60);
        } else {
            int timerX = 145;
            int timerY = 268;
            int timerW = 75;
            int timerH = 18;

            activeDisplay->fillRect(timerX, timerY, timerW, timerH, COLOR_BG);
            if (remainingSec <= 30) {
                activeDisplay->setTextColor(COLOR_RED);
            } else if (remainingSec <= 60) {
                activeDisplay->setTextColor(COLOR_ORANGE);
            } else {
                activeDisplay->setTextColor(COLOR_WHITE);
            }
            activeDisplay->setTextSize(2);
            activeDisplay->setCursor(timerX, timerY);
            activeDisplay->printf("%02d:%02d", remainingSec / 60, remainingSec % 60);
        }
    }

    // =========================================================================
    // SCREEN 3: PAYMENT SUCCESSFUL (WITH LIVE RELAY STATUS)
    // =========================================================================
    void showPaymentSuccessScreen(float amount, uint32_t durationSec, uint32_t remainingRelaySec = 3) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        // Header Banner
        activeDisplay->fillRect(0, 0, screenWidth, 42, COLOR_EMERALD);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(20, 12);
        activeDisplay->print("PAYMENT SUCCESSFUL!");

        // Center Confirmation Card
        activeDisplay->fillRoundRect(12, 54, screenWidth - 24, 140, 8, COLOR_PANEL);
        activeDisplay->drawRoundRect(12, 54, screenWidth - 24, 140, 8, COLOR_GREEN);

        if (screenWidth >= 300) {
            // Landscape Layout
            // Green OK badge
            activeDisplay->fillCircle(50, 124, 24, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_BLACK);
            activeDisplay->setCursor(39, 117);
            activeDisplay->print("OK");

            // Amount Paid
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->setCursor(88, 68);
            activeDisplay->printf("PAID: Rs. %.2f", amount);

            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(88, 94);
            activeDisplay->print("Transaction Captured & Verified");

            // LIVE RELAY STATUS BOX
            int badgeX = 88;
            int badgeY = 112;
            int badgeW = screenWidth - badgeX - 22;
            int badgeH = 34;
            activeDisplay->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, 0x0320); // Dark emerald
            activeDisplay->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 6, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(badgeX + 12, badgeY + 9);
            activeDisplay->printf("RELAY: OPEN [%us]", remainingRelaySec);

            // Subtext note
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_YELLOW);
            activeDisplay->setCursor(88, 158);
            activeDisplay->printf("Power Output Active for %u Seconds", durationSec);
        } else {
            // Portrait Layout
            activeDisplay->fillCircle(screenWidth / 2, 90, 22, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_BLACK);
            activeDisplay->setCursor((screenWidth / 2) - 11, 83);
            activeDisplay->print("OK");

            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->setCursor(30, 122);
            activeDisplay->printf("PAID: Rs. %.2f", amount);

            int badgeX = 24;
            int badgeY = 146;
            int badgeW = screenWidth - 48;
            int badgeH = 34;
            activeDisplay->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, 0x0320);
            activeDisplay->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 6, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(badgeX + 10, badgeY + 9);
            activeDisplay->printf("RELAY: OPEN [%us]", remainingRelaySec);
        }

        // Bottom Footer
        activeDisplay->fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_GREEN);
        activeDisplay->setCursor(16, screenHeight - 15);
        activeDisplay->print("STATUS: RELAY ENERGIZED / OPEN");
    }

    // Live update of relay remaining time on Payment Success screen (zero flicker)
    void updatePaymentSuccessRelayStatus(uint32_t remainingRelaySec) {
        if (!activeDisplay) return;

        if (screenWidth >= 300) {
            int badgeX = 88;
            int badgeY = 112;
            int badgeW = screenWidth - badgeX - 22;
            int badgeH = 34;

            activeDisplay->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, 0x0320);
            activeDisplay->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 6, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(badgeX + 12, badgeY + 9);

            if (remainingRelaySec > 0) {
                activeDisplay->printf("RELAY: OPEN [%us]", remainingRelaySec);
            } else {
                activeDisplay->print("RELAY: CLOSED [OFF]");
            }
        } else {
            int badgeX = 24;
            int badgeY = 146;
            int badgeW = screenWidth - 48;
            int badgeH = 34;

            activeDisplay->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, 0x0320);
            activeDisplay->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 6, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(badgeX + 10, badgeY + 9);

            if (remainingRelaySec > 0) {
                activeDisplay->printf("RELAY: OPEN [%us]", remainingRelaySec);
            } else {
                activeDisplay->print("RELAY: CLOSED [OFF]");
            }
        }
    }

    // Compatibility alias
    void showPaymentSuccess(float amount, uint32_t durationSec) {
        showPaymentSuccessScreen(amount, durationSec, durationSec);
    }

    // =========================================================================
    // SCREEN 4: PAYMENT FAILED
    // =========================================================================
    void showPaymentFailedScreen(const String& reason = "Payment Expired") {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        // Header Banner
        activeDisplay->fillRect(0, 0, screenWidth, 44, COLOR_CRIMSON);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(35, 14);
        activeDisplay->print("PAYMENT FAILED!");

        // Center Error Card
        activeDisplay->fillRoundRect(15, 60, screenWidth - 30, 135, 8, COLOR_PANEL);
        activeDisplay->drawRoundRect(15, 60, screenWidth - 30, 135, 8, COLOR_RED);

        // Red X badge
        activeDisplay->fillCircle(55, 127, 24, COLOR_RED);
        activeDisplay->setTextSize(3);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(46, 117);
        activeDisplay->print("X");

        // Error message
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_RED);
        activeDisplay->setCursor(95, 82);
        String displayReason = reason.length() > 0 ? reason : "Timeout / Expired";
        if (displayReason.length() > 18) displayReason = displayReason.substring(0, 18);
        activeDisplay->print(displayReason);

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(95, 114);
        activeDisplay->print("Transaction was not completed.");

        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->setCursor(95, 136);
        activeDisplay->print("No amount was charged.");

        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(95, 158);
        activeDisplay->print("Relay safely remained OFF.");

        // Bottom Footer
        activeDisplay->fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(20, screenHeight - 15);
        activeDisplay->print("Returning to Home screen...");
    }

    // Compatibility alias
    void showErrorScreen(const String& errorMsg) {
        showPaymentFailedScreen(errorMsg);
    }
};

#endif // DISPLAY_MANAGER_H
