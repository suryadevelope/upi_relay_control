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

    // =========================================================================
    // Small text/layout helpers (keep every element centered and aligned)
    // =========================================================================
    String formatAmount(float amount) const {
        long whole = (long)(amount + 0.5f);
        if (fabsf(amount - (float)whole) < 0.01f) {
            return "Rs." + String(whole);
        }
        return "Rs." + String(amount, 2);
    }

    void drawCentered(const String& text, int16_t x, int16_t w, int16_t y,
                      uint8_t size, uint16_t color) {
        if (!activeDisplay) return;
        int16_t x1, y1;
        uint16_t tw, th;
        activeDisplay->setTextSize(size);
        activeDisplay->getTextBounds(text.c_str(), 0, 0, &x1, &y1, &tw, &th);
        int16_t drawX = x + (w - (int16_t)tw) / 2 - x1;
        activeDisplay->setTextColor(color);
        activeDisplay->setCursor(drawX, y);
        activeDisplay->print(text);
    }

    // Shrinks the font when needed so the text never spills outside its box.
    void drawCenteredFit(const String& text, int16_t x, int16_t w, int16_t y,
                         uint8_t maxSize, uint16_t color) {
        if (!activeDisplay) return;
        uint8_t size = maxSize;
        int16_t x1, y1;
        uint16_t tw, th;
        while (size > 1) {
            activeDisplay->setTextSize(size);
            activeDisplay->getTextBounds(text.c_str(), 0, 0, &x1, &y1, &tw, &th);
            if ((int16_t)tw <= w) break;
            size--;
        }
        drawCentered(text, x, w, y, size, color);
    }

    String truncateToWidth(const String& text, uint16_t maxWidth, uint8_t size) {
        if (!activeDisplay) return text;
        String out = text;
        int16_t x1, y1;
        uint16_t w, h;
        while (out.length() > 0) {
            activeDisplay->setTextSize(size);
            activeDisplay->getTextBounds(out.c_str(), 0, 0, &x1, &y1, &w, &h);
            if (w <= maxWidth) break;
            out.remove(out.length() - 1);
        }
        return out;
    }

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
                        int ruleCount, const PaymentRule* rules, int selectedIndex = 0) {
        if (!activeDisplay) return;

        if (ruleCount < 0) ruleCount = 0;
        if (selectedIndex < 0) selectedIndex = 0;
        if (ruleCount > 0 && selectedIndex >= ruleCount) selectedIndex = ruleCount - 1;

        float amount = (ruleCount > 0 && rules != nullptr) ? rules[selectedIndex].amount : 1.0f;
        uint32_t durationSec = (ruleCount > 0 && rules != nullptr) ? rules[selectedIndex].durationSec : 3;
        String amountText = formatAmount(amount);
        String durationText = String(durationSec) + " sec power";

        String selectionText;
        if (ruleCount > 1) {
            selectionText = "AMOUNT " + String(selectedIndex + 1) + " OF " + String(ruleCount);
        } else if (ruleCount == 1) {
            selectionText = "SINGLE AMOUNT CONFIGURED";
        } else {
            selectionText = "DEFAULT AMOUNT";
        }

        activeDisplay->fillScreen(COLOR_BG);

        if (screenWidth >= 300) {
            // ================= Landscape (320x240) =================
            // Header bar
            activeDisplay->fillRect(0, 0, screenWidth, 36, COLOR_PANEL);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(14, 10);
            activeDisplay->print("PAYMENT GATEWAY");

            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(staConnected ? COLOR_GREEN : COLOR_ORANGE);
            activeDisplay->setCursor(screenWidth - 74, 15);
            activeDisplay->print(staConnected ? "ONLINE" : "OFFLINE");
            activeDisplay->fillCircle(screenWidth - 16, 18, 5,
                                      staConnected ? COLOR_GREEN : COLOR_ORANGE);

            // Selected amount card
            int cardX = 20, cardY = 46, cardW = screenWidth - 40, cardH = 96;
            activeDisplay->fillRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_PANEL);
            activeDisplay->drawRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_CYAN);

            drawCentered("SELECTED AMOUNT", cardX, cardW, cardY + 9, 1, COLOR_CYAN);
            drawCenteredFit(amountText, cardX + 12, cardW - 24, cardY + 22, 5, COLOR_GREEN);
            drawCentered(durationText, cardX, cardW, cardY + 64, 1, COLOR_YELLOW);
            drawCentered(selectionText, cardX, cardW, cardY + 79, 1, COLOR_GRAY);

            // Interaction cards (short press / long press)
            int actionY = 152, actionH = 54, actionGap = 10;
            int actionW = (screenWidth - 40 - actionGap) / 2;
            int leftX = 20, rightX = leftX + actionW + actionGap;

            activeDisplay->fillRoundRect(leftX, actionY, actionW, actionH, 7, COLOR_PANEL);
            activeDisplay->drawRoundRect(leftX, actionY, actionW, actionH, 7, COLOR_DARK_GRAY);
            drawCentered(ruleCount > 1 ? "SHORT PRESS" : "AMOUNT LOCKED",
                         leftX, actionW, actionY + 8, 1, COLOR_GRAY);
            drawCenteredFit(ruleCount > 1 ? "NEXT AMOUNT" : "READY",
                            leftX, actionW, actionY + 25, 2, COLOR_WHITE);

            activeDisplay->fillRoundRect(rightX, actionY, actionW, actionH, 7, 0x0320);
            activeDisplay->drawRoundRect(rightX, actionY, actionW, actionH, 7, COLOR_GREEN);
            drawCentered("HOLD 1.5s", rightX, actionW, actionY + 8, 1, COLOR_GREEN);
            drawCenteredFit("START PAY", rightX, actionW, actionY + 25, 2, COLOR_WHITE);

            // Footer: Wi-Fi status on the left, admin AP on the right
            activeDisplay->fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(staConnected ? COLOR_CYAN : COLOR_ORANGE);
            activeDisplay->setCursor(12, screenHeight - 15);
            if (staConnected && staIP.length() > 0) {
                activeDisplay->printf("WiFi: %s", staIP.c_str());
            } else {
                activeDisplay->print("WiFi: OFFLINE");
            }

            String apLabel = truncateToWidth("AP: " + apSSID, 118, 1);
            int16_t x1, y1;
            uint16_t tw, th;
            activeDisplay->setTextSize(1);
            activeDisplay->getTextBounds(apLabel.c_str(), 0, 0, &x1, &y1, &tw, &th);
            activeDisplay->setTextColor(COLOR_GRAY);
            activeDisplay->setCursor(screenWidth - 12 - (int16_t)tw - x1, screenHeight - 15);
            activeDisplay->print(apLabel);
        } else {
            // ================= Portrait (240x320) =================
            activeDisplay->fillRect(0, 0, screenWidth, 38, COLOR_PANEL);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_WHITE);
            activeDisplay->setCursor(12, 11);
            activeDisplay->print("PAYMENT GATEWAY");

            activeDisplay->fillCircle(screenWidth - 16, 19, 5,
                                      staConnected ? COLOR_GREEN : COLOR_ORANGE);

            // Selected amount card
            int cardX = 14, cardY = 48, cardW = screenWidth - 28, cardH = 122;
            activeDisplay->fillRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_PANEL);
            activeDisplay->drawRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_CYAN);

            drawCentered("SELECTED AMOUNT", cardX, cardW, cardY + 10, 1, COLOR_CYAN);
            drawCenteredFit(amountText, cardX + 10, cardW - 20, cardY + 26, 5, COLOR_GREEN);
            drawCentered(durationText, cardX, cardW, cardY + 68, 1, COLOR_YELLOW);
            drawCentered(selectionText, cardX, cardW, cardY + 88, 1, COLOR_GRAY);

            // Short press hint
            int actionX = 14, actionW = screenWidth - 28;
            int shortY = 182, shortH = 38;
            activeDisplay->fillRoundRect(actionX, shortY, actionW, shortH, 7, COLOR_PANEL);
            activeDisplay->drawRoundRect(actionX, shortY, actionW, shortH, 7, COLOR_DARK_GRAY);
            drawCenteredFit(ruleCount > 1 ? "SHORT PRESS: CHANGE AMOUNT" : "SINGLE AMOUNT CONFIGURED",
                            actionX, actionW, shortY + 15, 1, COLOR_WHITE);

            // Long press to pay
            int holdY = 230, holdH = 50;
            activeDisplay->fillRoundRect(actionX, holdY, actionW, holdH, 7, 0x0320);
            activeDisplay->drawRoundRect(actionX, holdY, actionW, holdH, 7, COLOR_GREEN);
            drawCentered("HOLD 1.5s", actionX, actionW, holdY + 7, 1, COLOR_GREEN);
            drawCenteredFit("START PAYMENT", actionX, actionW, holdY + 24, 2, COLOR_WHITE);

            // Footer
            activeDisplay->fillRect(0, screenHeight - 25, screenWidth, 25, COLOR_PANEL);
            activeDisplay->setTextSize(1);
            activeDisplay->setTextColor(staConnected ? COLOR_CYAN : COLOR_ORANGE);
            activeDisplay->setCursor(10, screenHeight - 16);
            if (staConnected && staIP.length() > 0) {
                activeDisplay->printf("WiFi: %s", staIP.c_str());
            } else {
                String apFooter = truncateToWidth("AP: " + apSSID, screenWidth - 20, 1);
                activeDisplay->print(apFooter);
            }
        }
    }

    // Alias for compatibility
    void showReadyScreen(const String& apSSID, const String& staIP, bool staConnected,
                         int ruleCount, const PaymentRule* rules, int selectedIndex = 0) {
        showHomeScreen(apSSID, staIP, staConnected, ruleCount, rules, selectedIndex);
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
        drawCentered("PAYMENT SUCCESSFUL!", 0, screenWidth, 13, 2, COLOR_WHITE);

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
            drawCenteredFit("PAID: " + formatAmount(amount),
                            88, screenWidth - 88 - 22, 68, 2, COLOR_GREEN);
            drawCentered("Transaction Captured & Verified",
                         88, screenWidth - 88 - 22, 94, 1, COLOR_WHITE);

            // LIVE RELAY STATUS BOX
            int badgeX = 88;
            int badgeY = 112;
            int badgeW = screenWidth - badgeX - 22;
            int badgeH = 34;
            activeDisplay->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, 0x0320); // Dark emerald
            activeDisplay->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 6, COLOR_GREEN);
            drawCenteredFit("RELAY: OPEN [" + String(remainingRelaySec) + "s]",
                            badgeX, badgeW, badgeY + 9, 2, COLOR_WHITE);

            // Subtext note
            drawCentered("Power Output Active for " + String(durationSec) + " Seconds",
                         88, screenWidth - 88 - 22, 158, 1, COLOR_YELLOW);
        } else {
            // Portrait Layout
            activeDisplay->fillCircle(screenWidth / 2, 90, 22, COLOR_GREEN);
            activeDisplay->setTextSize(2);
            activeDisplay->setTextColor(COLOR_BLACK);
            activeDisplay->setCursor((screenWidth / 2) - 11, 83);
            activeDisplay->print("OK");

            drawCenteredFit("PAID: " + formatAmount(amount),
                            12, screenWidth - 24, 122, 2, COLOR_GREEN);

            int badgeX = 12;
            int badgeY = 146;
            int badgeW = screenWidth - 24;
            int badgeH = 34;
            activeDisplay->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, 0x0320);
            activeDisplay->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 6, COLOR_GREEN);
            drawCenteredFit("RELAY: OPEN [" + String(remainingRelaySec) + "s]",
                            badgeX, badgeW, badgeY + 9, 2, COLOR_WHITE);

            // Subtext note (keeps the portrait layout balanced)
            drawCentered("Power Output Active for " + String(durationSec) + " Seconds",
                         12, screenWidth - 24, 208, 1, COLOR_YELLOW);
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

            String relayText = (remainingRelaySec > 0)
                                   ? "RELAY: OPEN [" + String(remainingRelaySec) + "s]"
                                   : "RELAY: CLOSED [OFF]";
            drawCenteredFit(relayText, badgeX, badgeW, badgeY + 9, 2, COLOR_WHITE);
        } else {
            int badgeX = 12;
            int badgeY = 146;
            int badgeW = screenWidth - 24;
            int badgeH = 34;

            activeDisplay->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, 0x0320);
            activeDisplay->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 6, COLOR_GREEN);

            String relayText = (remainingRelaySec > 0)
                                   ? "RELAY: OPEN [" + String(remainingRelaySec) + "s]"
                                   : "RELAY: CLOSED [OFF]";
            drawCenteredFit(relayText, badgeX, badgeW, badgeY + 9, 2, COLOR_WHITE);
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
        drawCentered("PAYMENT FAILED!", 0, screenWidth, 14, 2, COLOR_WHITE);

        // Center Error Card (fits both landscape and portrait)
        int cardX = 15;
        int cardY = (screenWidth >= 300) ? 54 : 56;
        int cardW = screenWidth - 30;
        int cardH = screenHeight - cardY - 32;
        activeDisplay->fillRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_PANEL);
        activeDisplay->drawRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_RED);

        // Vertically center the content block inside the card
        const int contentH = 156;
        int contentY = cardY + (cardH - contentH) / 2;

        // Red X badge
        int badgeCX = screenWidth / 2;
        int badgeCY = contentY + 30;
        activeDisplay->fillCircle(badgeCX, badgeCY, 24, COLOR_RED);
        drawCentered("X", badgeCX - 24, 48, badgeCY - 12, 3, COLOR_WHITE);

        // Error message
        String displayReason = reason.length() > 0 ? reason : "Timeout / Expired";
        if (displayReason.length() > 24) displayReason = displayReason.substring(0, 24);
        drawCenteredFit(displayReason, cardX + 10, cardW - 20, contentY + 66, 2, COLOR_RED);

        drawCentered("Transaction was not completed.", cardX + 6, cardW - 12, contentY + 98, 1, COLOR_WHITE);
        drawCentered("No amount was charged.", cardX + 6, cardW - 12, contentY + 114, 1, COLOR_GRAY);
        drawCentered("Relay safely remained OFF.", cardX + 6, cardW - 12, contentY + 134, 1, COLOR_YELLOW);

        // Bottom Footer
        activeDisplay->fillRect(0, screenHeight - 22, screenWidth, 22, COLOR_PANEL);
        drawCentered("Returning to Home screen...", 0, screenWidth, screenHeight - 15, 1, COLOR_YELLOW);
    }

    // Compatibility alias
    void showErrorScreen(const String& errorMsg) {
        showPaymentFailedScreen(errorMsg);
    }
};

#endif // DISPLAY_MANAGER_H
