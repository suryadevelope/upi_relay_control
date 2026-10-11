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
#define COLOR_BG          0x0841  // Very dark slate blue
#define COLOR_PANEL       0x18E3  // Dark slate
#define COLOR_WHITE       0xFFFF
#define COLOR_BLACK       0x0000
#define COLOR_GREEN       0x07E0
#define COLOR_EMERALD     0x2E6B
#define COLOR_RED         0xF800
#define COLOR_CRIMSON     0xD8A7
#define COLOR_BLUE        0x001F
#define COLOR_CYAN        0x07FF
#define COLOR_YELLOW      0xFFE0
#define COLOR_ORANGE      0xFD20
#define COLOR_GRAY        0x7BEF
#define COLOR_DARK_GRAY   0x39E7

class DisplayManager {
private:
    Adafruit_ST7789* st7789Display = nullptr;
    Adafruit_ILI9341* ili9341Display = nullptr;
    Adafruit_GFX* activeDisplay = nullptr;

    int screenWidth = 320;
    int screenHeight = 240;
    int displayType = DISPLAY_ST7789;
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

        if (displayType == DISPLAY_ILI9341) {
            Serial.printf("[DISPLAY] Initializing ILI9341 (CS=%d, DC=%d, RST=%d)...\n",
                          settings.tftCS, settings.tftDC, settings.tftRST);
            ili9341Display = new Adafruit_ILI9341(settings.tftCS, settings.tftDC, settings.tftRST);
            ili9341Display->begin();
            ili9341Display->setRotation(rotation);
            activeDisplay = ili9341Display;
        } else {
            Serial.printf("[DISPLAY] Initializing ST7789 (CS=%d, DC=%d, RST=%d)...\n",
                          settings.tftCS, settings.tftDC, settings.tftRST);
            // ST7789 2.4/2.5-inch typically 240x320 or 240x240
            st7789Display = new Adafruit_ST7789(settings.tftCS, settings.tftDC, settings.tftRST);
            st7789Display->init(240, 320);
            st7789Display->setRotation(rotation);
            activeDisplay = st7789Display;
        }

        if (activeDisplay) {
            screenWidth = activeDisplay->width();
            screenHeight = activeDisplay->height();
            activeDisplay->fillScreen(COLOR_BLACK);
        }
    }

    void showBootScreen() {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        // Header Banner
        activeDisplay->fillRect(0, 0, screenWidth, 45, COLOR_PANEL);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->setCursor(20, 15);
        activeDisplay->print("SMART GATEWAY");

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(screenWidth - 65, 18);
        activeDisplay->print("v2.0 ESP");

        // Subtitle & Status
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(25, 75);
        activeDisplay->print("UPI & Razorpay Relay Controller");

        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->setCursor(25, 110);
        activeDisplay->print("Initializing Non-Volatile Storage...");

        activeDisplay->setCursor(25, 130);
        activeDisplay->print("Setting up Wi-Fi AP + Station...");

        activeDisplay->setCursor(25, 150);
        activeDisplay->print("Relay Pin configured (OFF by default)...");

        // Progress bar simulation
        activeDisplay->drawRoundRect(25, 185, screenWidth - 50, 14, 4, COLOR_CYAN);
        activeDisplay->fillRoundRect(27, 187, screenWidth - 54, 10, 3, COLOR_CYAN);

        activeDisplay->setTextColor(COLOR_GREEN);
        activeDisplay->setCursor(25, 212);
        activeDisplay->print("Booting system services...");
    }

    void showReadyScreen(const String& apSSID, const String& staIP, bool staConnected, 
                         int ruleCount, const PaymentRule* rules) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        // Top Header
        activeDisplay->fillRect(0, 0, screenWidth, 40, COLOR_PANEL);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(15, 12);
        activeDisplay->print("PAYMENT GATEWAY");

        // Status Indicator
        if (staConnected) {
            activeDisplay->fillCircle(screenWidth - 25, 20, 7, COLOR_GREEN);
        } else {
            activeDisplay->fillCircle(screenWidth - 25, 20, 7, COLOR_ORANGE);
        }

        // Connection Info Box
        activeDisplay->fillRoundRect(10, 48, screenWidth - 20, 52, 6, COLOR_PANEL);
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->setCursor(18, 55);
        activeDisplay->print("Hotspot: ");
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->print(apSSID);
        activeDisplay->print(" (192.168.4.1)");

        activeDisplay->setCursor(18, 72);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->print("Internet: ");
        if (staConnected) {
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->printf("Connected (%s)", staIP.c_str());
        } else {
            activeDisplay->setTextColor(COLOR_ORANGE);
            activeDisplay->print("Station Disconnected (AP only)");
        }

        activeDisplay->setCursor(18, 88);
        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->print("Open admin portal on phone to initiate payment");

        // Available Price Rules
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(15, 108);
        activeDisplay->print("CONFIGURED PRICING RULES:");

        int startY = 125;
        int maxShow = ruleCount > 4 ? 4 : ruleCount;
        for (int i = 0; i < maxShow; i++) {
            int cardW = (screenWidth - 30) / (maxShow > 0 ? maxShow : 1);
            int cardX = 10 + (i * cardW);

            activeDisplay->fillRoundRect(cardX, startY, cardW - 6, 65, 5, COLOR_PANEL);
            activeDisplay->drawRoundRect(cardX, startY, cardW - 6, 65, 5, COLOR_CYAN);

            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_GREEN);
            activeDisplay->setCursor(cardX + 8, startY + 12);
            activeDisplay->printf("Rs.%d", (int)rules[i].amount);

            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(cardX + 8, startY + 42);
            activeDisplay->printf("%u Sec", rules[i].durationSec);
        }

        // Bottom Footer
        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->setTextSize(1);
        activeDisplay->setCursor(20, screenHeight - 18);
        activeDisplay->print("State: READY | Relay: SAFE OFF");
    }

    void showQRScreen(const String& qrPayload, float amount, uint32_t durationSec, 
                      const String& qrId, uint32_t remainingSec) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        // Header with Amount
        activeDisplay->fillRect(0, 0, screenWidth, 38, COLOR_PANEL);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_GREEN);
        activeDisplay->setCursor(15, 10);
        activeDisplay->printf("PAY Rs. %.2f", amount);

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(screenWidth - 110, 14);
        activeDisplay->printf("Time: %u Sec", durationSec);

        // QR Code Container
        int qrCenterY = (screenHeight / 2) + 6;
        int qrCenterX = (screenWidth / 2) - 35;
        int qrPixelSize = screenHeight - 65; // ~175px

        QRCodeRenderer::renderToDisplay(activeDisplay, qrPayload, qrCenterX, qrCenterY, 
                                        qrPixelSize, COLOR_BLACK, COLOR_WHITE);

        // Right side info panel
        int infoX = (screenWidth / 2) + 65;
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->setCursor(infoX, 55);
        activeDisplay->print("Scan via:");

        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(infoX, 72);
        activeDisplay->print("- Google Pay");
        activeDisplay->setCursor(infoX, 87);
        activeDisplay->print("- PhonePe");
        activeDisplay->setCursor(infoX, 102);
        activeDisplay->print("- Paytm / UPI");

        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(infoX, 130);
        activeDisplay->print("Time Left:");
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(infoX, 146);
        activeDisplay->printf("%02d:%02d", remainingSec / 60, remainingSec % 60);

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->setCursor(infoX, 180);
        activeDisplay->print("Razorpay");
        activeDisplay->setCursor(infoX, 195);
        activeDisplay->print("Verified QR");

        // Bottom status line
        activeDisplay->fillRect(0, screenHeight - 20, screenWidth, 20, COLOR_PANEL);
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->setCursor(10, screenHeight - 14);
        activeDisplay->print("Awaiting payment verification from backend...");
    }

    void showVerifyingScreen(float amount) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        activeDisplay->fillRect(0, 0, screenWidth, 40, COLOR_PANEL);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(20, 12);
        activeDisplay->print("VERIFYING PAYMENT");

        activeDisplay->fillRoundRect(20, 60, screenWidth - 40, 120, 8, COLOR_PANEL);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(40, 85);
        activeDisplay->printf("Amount: Rs. %.2f", amount);

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->setCursor(40, 120);
        activeDisplay->print("Authenticating Razorpay captured state...");

        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->setCursor(40, 145);
        activeDisplay->print("Checking digital signature & session token");

        // Bottom status
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(25, 205);
        activeDisplay->print("Please wait, securing transaction...");
    }

    void showPaymentSuccess(float amount, uint32_t durationSec) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_EMERALD);

        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(20, 30);
        activeDisplay->print("PAYMENT VERIFIED!");

        activeDisplay->fillRoundRect(15, 70, screenWidth - 30, 110, 8, COLOR_BLACK);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_GREEN);
        activeDisplay->setCursor(35, 90);
        activeDisplay->printf("Captured: Rs. %.2f", amount);

        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(35, 125);
        activeDisplay->printf("Granted: %u Seconds", durationSec);

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(35, 155);
        activeDisplay->print("Signature confirmed. Activating relay...");
    }

    void showRelayActiveScreen(uint32_t remainingSec, uint32_t totalSec, float amount) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        // Header
        activeDisplay->fillRect(0, 0, screenWidth, 42, COLOR_EMERALD);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(20, 12);
        activeDisplay->print("RELAY ACTIVE [ON]");

        // Big countdown timer
        activeDisplay->fillRoundRect(20, 55, screenWidth - 40, 115, 8, COLOR_PANEL);

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->setCursor(35, 68);
        activeDisplay->printf("SERVICE IN PROGRESS (Rs. %.2f)", amount);

        // Huge remaining seconds
        activeDisplay->setTextSize(5);
        activeDisplay->setTextColor(COLOR_GREEN);
        activeDisplay->setCursor(70, 95);
        activeDisplay->printf("%02u s", remainingSec);

        // Progress bar
        float progress = 0.0f;
        if (totalSec > 0) {
            progress = (float)(totalSec - remainingSec) / (float)totalSec;
            if (progress > 1.0f) progress = 1.0f;
        }

        int barW = screenWidth - 60;
        activeDisplay->drawRoundRect(30, 185, barW, 16, 4, COLOR_CYAN);
        int fillW = (int)((barW - 4) * progress);
        if (fillW > 0) {
            activeDisplay->fillRoundRect(32, 187, fillW, 12, 3, COLOR_GREEN);
        }

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->setCursor(30, 212);
        activeDisplay->printf("Automatic power cut in %u seconds", remainingSec);
    }

    void showCompleteScreen() {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);

        activeDisplay->fillRect(0, 0, screenWidth, 45, COLOR_PANEL);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_CYAN);
        activeDisplay->setCursor(20, 14);
        activeDisplay->print("SESSION COMPLETE");

        activeDisplay->fillRoundRect(20, 65, screenWidth - 40, 110, 8, COLOR_PANEL);
        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_GREEN);
        activeDisplay->setCursor(40, 85);
        activeDisplay->print("RELAY SWITCHED OFF");

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(40, 120);
        activeDisplay->print("Load safely disconnected.");
        activeDisplay->setCursor(40, 138);
        activeDisplay->print("Transaction permanently logged to NVS.");

        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(40, 195);
        activeDisplay->print("Returning to READY state...");
    }

    void showErrorScreen(const String& errorMsg) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_CRIMSON);

        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(20, 25);
        activeDisplay->print("TRANSACTION ERROR");

        activeDisplay->fillRoundRect(15, 65, screenWidth - 30, 125, 8, COLOR_BLACK);
        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_RED);
        activeDisplay->setCursor(30, 85);
        activeDisplay->print("REASON:");

        activeDisplay->setTextSize(2);
        activeDisplay->setTextColor(COLOR_WHITE);
        activeDisplay->setCursor(30, 105);
        activeDisplay->print(errorMsg.substring(0, 22));

        activeDisplay->setTextSize(1);
        activeDisplay->setTextColor(COLOR_GRAY);
        activeDisplay->setCursor(30, 145);
        activeDisplay->print("Relay remained safely OFF.");
        activeDisplay->setCursor(30, 162);
        activeDisplay->print("No unverified activation permitted.");

        activeDisplay->setTextColor(COLOR_YELLOW);
        activeDisplay->setCursor(30, 205);
        activeDisplay->print("Press reset or wait to return to Ready.");
    }
};

#endif // DISPLAY_MANAGER_H
