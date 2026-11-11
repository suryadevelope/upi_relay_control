#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>   // Driver, pins, rotation setup come from the TFT_* build flags in platformio.ini
#include "Config.h"
#include "QRCodeRenderer.h"

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON 1
#endif

// ----------------------------------------------------------------------------
// Outdoor (direct sunlight) palette: pure black background, white / yellow /
// bright green text, bright colour bands. No low-contrast slate panels.
// ----------------------------------------------------------------------------
#define COLOR_BG          0x0000  // Black background
#define COLOR_PANEL       0x0000  // Cards are outlined in colour, not filled
#define COLOR_WHITE       0xFFFF
#define COLOR_BLACK       0x0000
#define COLOR_GREEN       0x07E0  // Bright green
#define COLOR_EMERALD     0x07E0  // Success banner
#define COLOR_RED         0xF800  // Bright red
#define COLOR_CRIMSON     0xF800  // Failure banner
#define COLOR_CYAN        0x07FF
#define COLOR_YELLOW      0xFFE0
#define COLOR_ORANGE      0xFD20
#define COLOR_GRAY        0xC618  // Light grey, still readable in sun
#define COLOR_DARK_GRAY   0x7BEF  // Mid grey, used only for dividers

// Text sizes (built-in 6x8 font scaled by size): size 2 = 12x16 px,
// size 3 = 18x24 px, size 4 = 24x32 px, size 5 = 30x40 px.
// No text is drawn smaller than size 2.

class DisplayManager {
private:
    static constexpr int HEADER_H = 40;
    static constexpr int FOOTER_H = 30;
    static constexpr uint8_t BL_CHANNEL = 0;     // LEDC channel for backlight PWM
    static constexpr uint8_t BL_FULL = 255;      // Full brightness (default)

    TFT_eSPI tft;                        // ST7789 240x320 panel, used as 320x240 landscape
    TFT_eSPI* activeDisplay = nullptr;   // set by begin() once tft is initialised

    int screenWidth = 320;
    int screenHeight = 240;
    int rotation = 1;
    int blPin = -1;
    uint8_t brightness = BL_FULL;

    // ------------------------------------------------------------------------
    // Text and layout helpers
    // ------------------------------------------------------------------------
    // Built-in font: 6 px per character cell, 8 px tall, scaled by size.
    static int textW(const String& s, uint8_t size) {
        return (int)s.length() * 6 * size;
    }
    static int textH(uint8_t size) { return 8 * size; }

    String formatAmount(float amount) const {
        long whole = (long)(amount + 0.5f);
        if (fabsf(amount - (float)whole) < 0.01f) {
            return "Rs." + String(whole);
        }
        return "Rs." + String(amount, 2);
    }

    void text(const String& s, int x, int y, uint8_t size, uint16_t fg) {
        activeDisplay->setTextSize(size);
        activeDisplay->setTextColor(fg);
        activeDisplay->setCursor(x, y);
        activeDisplay->print(s);
    }

    void textCentered(const String& s, int x, int w, int y, uint8_t size, uint16_t fg) {
        text(s, x + (w - textW(s, size)) / 2, y, size, fg);
    }

    // Largest size (<= maxSize, never below 2) whose width fits in w
    static uint8_t fitSize(const String& s, int w, uint8_t maxSize) {
        uint8_t size = maxSize;
        while (size > 2 && textW(s, size) > w) size--;
        return size;
    }

    void textFit(const String& s, int x, int w, int y, uint8_t maxSize, uint16_t fg) {
        textCentered(s, x, w, y, fitSize(s, w, maxSize), fg);
    }

    // Drops characters from the end until the text fits (size 2 minimum)
    static String truncateToWidth(const String& s, int maxW, uint8_t size) {
        String out = s;
        while (out.length() > 0 && textW(out, size) > maxW) {
            out.remove(out.length() - 1);
        }
        return out;
    }

    // Two-pixel outline, so the card edge stays visible in bright light
    void drawCard(int x, int y, int w, int h, uint16_t colour) {
        activeDisplay->drawRoundRect(x, y, w, h, 6, colour);
        activeDisplay->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, colour);
    }

    void clearBody() {
        activeDisplay->fillRect(0, HEADER_H, screenWidth,
                                screenHeight - HEADER_H - FOOTER_H, COLOR_BG);
    }

    // Solid colour band across the top with a centred title
    void drawHeader(const String& title, uint16_t bg, uint16_t fg) {
        activeDisplay->fillRect(0, 0, screenWidth, HEADER_H, bg);
        textFit(title, 6, screenWidth - 12, (HEADER_H - textH(3)) / 2, 3, fg);
    }

    // Footer: three key hints. left/right are pinned to the edges, centre sits
    // in the gap between them. Pass an empty string to omit an item.
    void drawFooter(const String& left, const String& centre, const String& right,
                    uint16_t centreColour) {
        const int y = screenHeight - FOOTER_H;
        activeDisplay->drawFastHLine(0, y, screenWidth, COLOR_DARK_GRAY);
        activeDisplay->fillRect(0, y + 1, screenWidth, FOOTER_H - 1, COLOR_BG);
        const int ty = y + (FOOTER_H - textH(2)) / 2 + 1;

        int leftEnd = 6;
        if (left.length()) {
            text(left, 6, ty, 2, COLOR_YELLOW);
            leftEnd = 6 + textW(left, 2);
        }
        int rightStart = screenWidth - 6;
        if (right.length()) {
            rightStart = screenWidth - 6 - textW(right, 2);
            text(right, rightStart, ty, 2, COLOR_YELLOW);
        }
        if (centre.length()) {
            int cx = leftEnd + (rightStart - leftEnd - textW(centre, 2)) / 2;
            text(centre, cx, ty, 2, centreColour);
        }
    }

    // Backlight: PWM on the backlight pin. Level 0..255.
    void writeBacklight(uint8_t level) {
        if (blPin < 0) return;
        ledcWrite(BL_CHANNEL, TFT_BACKLIGHT_ON ? level : (255 - level));
    }

public:
    DisplayManager() {}

    // Pins, driver and rotation are fixed by the TFT_* build flags in platformio.ini,
    // so the tftCS/tftDC/tftRST settings are informational only. TFT_eSPI owns the SPI bus.
    void begin(const DeviceSettings& settings) {
        rotation = settings.tftRotation;
        blPin = settings.tftBL;

        if (blPin >= 0) {
            ledcSetup(BL_CHANNEL, 5000, 8);      // 5 kHz, 8-bit PWM: flicker-free
            ledcAttachPin(blPin, BL_CHANNEL);
            writeBacklight(brightness);
        }

        Serial.printf("[DISPLAY] Initializing ST7789 240x320 via TFT_eSPI (CS=%d, DC=%d, RST=%d)...\n",
                      TFT_CS, TFT_DC, TFT_RST);
        tft.init();
        tft.setRotation(rotation);
        activeDisplay = &tft;

        screenWidth = tft.width();
        screenHeight = tft.height();
        tft.fillScreen(COLOR_BLACK);
        Serial.printf("[DISPLAY] TFT Display Ready: %dx%d (Rotation: %d), backlight %u/255\n",
                      screenWidth, screenHeight, rotation, brightness);
    }

    // 0..255. Full brightness is the default; lower it indoors if the screen is too bright.
    void setBrightness(uint8_t level) {
        brightness = level;
        writeBacklight(level);
    }

    uint8_t getBrightness() const { return brightness; }

    // =========================================================================
    // HOME MENU: amount cards, UP / DOWN select, ENTER pays, RIGHT shows info
    // =========================================================================
    void showMenuHome(const String& deviceName, bool serviceEnabled, bool staConnected,
                      const String& staIP, int ruleCount, const PaymentRule* rules,
                      int selectedIndex) {
        if (!activeDisplay) return;
        const int w = screenWidth;

        // ---- Header: name left, Wi-Fi and service pills right ----
        activeDisplay->fillRect(0, 0, w, HEADER_H, COLOR_WHITE);

        const int pillH = 24;
        const int pillY = (HEADER_H - pillH) / 2;

        const String svc = serviceEnabled ? "ON" : "STOP";
        const int svcW = textW(svc, 2) + 16;
        const int svcX = w - 6 - svcW;
        activeDisplay->fillRoundRect(svcX, pillY, svcW, pillH, 5,
                                     serviceEnabled ? COLOR_GREEN : COLOR_RED);
        textCentered(svc, svcX, svcW, pillY + 4, 2, serviceEnabled ? COLOR_BLACK : COLOR_WHITE);

        const String net = staConnected ? "WIFI" : "OFF";
        const int netW = textW(net, 2) + 16;
        const int netX = svcX - 6 - netW;
        activeDisplay->fillRoundRect(netX, pillY, netW, pillH, 5,
                                     staConnected ? COLOR_GREEN : COLOR_RED);
        textCentered(net, netX, netW, pillY + 4, 2, staConnected ? COLOR_BLACK : COLOR_WHITE);

        const int titleMax = netX - 6 - 8;
        const uint8_t titleSize = fitSize(deviceName, titleMax, 3);
        text(truncateToWidth(deviceName, titleMax, titleSize), 8,
             (HEADER_H - textH(titleSize)) / 2, titleSize, COLOR_BLACK);

        // ---- Body: 3 large amount cards (scrolls for more) ----
        clearBody();
        const int bodyTop = HEADER_H + 4;
        const int bodyBottom = screenHeight - FOOTER_H - 4;
        const int rowH = 50;
        const int rowGap = 6;
        const int listX = 6;
        const int listW = w - 6 - 22;             // right 22 px is the scroll gutter
        int maxRows = (bodyBottom - bodyTop + rowGap) / (rowH + rowGap);
        if (maxRows < 1) maxRows = 1;

        if (ruleCount <= 0 || rules == nullptr) {
            textFit("NO CARDS", 6, w - 12, bodyTop + 24, 4, COLOR_YELLOW);
            textFit("Add cards in admin app", 6, w - 12, bodyTop + 88, 2, COLOR_GRAY);
        } else {
            if (selectedIndex < 0) selectedIndex = 0;
            if (selectedIndex >= ruleCount) selectedIndex = ruleCount - 1;

            // Keep the selected card near the middle of the visible window
            int first = selectedIndex - maxRows / 2;
            if (first > ruleCount - maxRows) first = ruleCount - maxRows;
            if (first < 0) first = 0;

            for (int row = 0; row < maxRows && (first + row) < ruleCount; row++) {
                const int idx = first + row;
                const int y = bodyTop + row * (rowH + rowGap);
                const bool sel = (idx == selectedIndex);

                if (sel) {
                    // Selected: solid yellow card with black text (reads best in sun)
                    activeDisplay->fillRoundRect(listX, y, listW, rowH, 6, COLOR_YELLOW);
                } else {
                    activeDisplay->fillRoundRect(listX, y, listW, rowH, 6, COLOR_BG);
                    drawCard(listX, y, listW, rowH, COLOR_GRAY);
                }

                const String dur = String(rules[idx].durationSec) + "s";
                const int durW = textW(dur, 2);
                const String amount = formatAmount(rules[idx].amount);
                const int amountMax = listW - 24 - durW - 12;
                const uint8_t amountSize = fitSize(amount, amountMax, 4);
                const uint16_t ink = sel ? COLOR_BLACK : COLOR_WHITE;

                text(amount, listX + 12, y + (rowH - textH(amountSize)) / 2, amountSize, ink);
                text(dur, listX + listW - 12 - durW, y + (rowH - textH(2)) / 2, 2,
                     sel ? COLOR_BLACK : COLOR_YELLOW);
            }

            // Scroll markers in the gutter: more cards above / below the window
            const int gx = w - 11;
            if (first > 0) {
                activeDisplay->fillTriangle(gx - 6, bodyTop + 10, gx + 6, bodyTop + 10,
                                            gx, bodyTop + 2, COLOR_YELLOW);
            }
            if (first + maxRows < ruleCount) {
                const int by = bodyBottom - 2;
                activeDisplay->fillTriangle(gx - 6, by - 10, gx + 6, by - 10,
                                            gx, by, COLOR_YELLOW);
            }
        }

        // ---- Footer: the three keys ----
        if (serviceEnabled) {
            drawFooter("UP/DN", "ENTER:PAY", "RIGHT:INFO", COLOR_GREEN);
        } else {
            drawFooter("UP/DN", "PAY OFF", "RIGHT:INFO", COLOR_RED);
        }
    }

    // =========================================================================
    // CONFIRM: big amount, shown before the QR is generated
    // =========================================================================
    void showConfirmScreen(float amount, uint32_t durationSec) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("CONFIRM PAYMENT", COLOR_WHITE, COLOR_BLACK);

        textCentered("AMOUNT", 0, screenWidth, 50, 2, COLOR_GRAY);
        textFit(formatAmount(amount), 10, screenWidth - 20, 74, 5, COLOR_GREEN);
        textFit("Relay ON for " + String(durationSec) + " seconds", 6, screenWidth - 12,
                134, 2, COLOR_YELLOW);

        drawFooter("LEFT:BACK", "", "ENTER:SHOW QR", COLOR_WHITE);
    }

    // =========================================================================
    // INFO: device identity and live status (RIGHT from home)
    // =========================================================================
    void showInfoScreen(const String& deviceName, const String& fwVersion, const String& apSSID,
                        bool staConnected, const String& staIP, bool serviceEnabled, bool relayOn) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("DEVICE INFO", COLOR_WHITE, COLOR_BLACK);

        int y = HEADER_H + 8;
        const int step = 28;
        const int valueX = 112;   // clears the widest label ("Firmware" = 96 px)
        const int valueMax = screenWidth - valueX - 6;
        auto row = [&](const char* label, const String& value, uint16_t colour) {
            text(label, 8, y, 2, COLOR_GRAY);
            text(truncateToWidth(value, valueMax, 2), valueX, y, 2, colour);
            y += step;
        };

        row("Name", deviceName, COLOR_WHITE);
        row("Firmware", fwVersion, COLOR_WHITE);
        row("Wi-Fi", (staConnected && staIP.length() > 0) ? staIP : String("offline"),
            staConnected ? COLOR_GREEN : COLOR_RED);
        row("Hotspot", apSSID, COLOR_CYAN);
        row("Service", serviceEnabled ? String("ENABLED") : String("STOPPED"),
            serviceEnabled ? COLOR_GREEN : COLOR_RED);
        row("Relay", relayOn ? String("ON") : String("OFF"), relayOn ? COLOR_YELLOW : COLOR_GRAY);

        drawFooter("LEFT:BACK", "", "", COLOR_WHITE);
    }

    // =========================================================================
    // NOTICE: full-screen message that closes itself
    // =========================================================================
    void showNoticeScreen(const String& title, const String& message, uint16_t colour) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        const uint16_t ink = (colour == COLOR_RED) ? COLOR_WHITE : COLOR_BLACK;
        drawHeader(title, colour, ink);
        textFit(message, 8, screenWidth - 16, HEADER_H + (screenHeight - HEADER_H) / 2 - 16,
                3, COLOR_WHITE);
    }

    // =========================================================================
    // LOADING: shown from ENTER until the cloud returns the QR (bar keeps moving)
    // =========================================================================
    static constexpr int LOAD_BAR_X = 20;
    static constexpr int LOAD_BAR_Y = 92;
    static constexpr int LOAD_BAR_H = 36;

    void showLoadingScreen(float amount) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("PREPARING QR", COLOR_WHITE, COLOR_BLACK);

        textFit(formatAmount(amount), 6, screenWidth - 12, HEADER_H + 8, 4, COLOR_GREEN);
        textCentered("Contacting bank...", 0, screenWidth, LOAD_BAR_Y - 30, 2, COLOR_YELLOW);

        const int bw = screenWidth - 2 * LOAD_BAR_X;
        drawCard(LOAD_BAR_X, LOAD_BAR_Y, bw, LOAD_BAR_H, COLOR_WHITE);
        updateLoadingBar(0);
    }

    // Moves the sliding block and the elapsed counter. Called every ~50 ms.
    void updateLoadingBar(uint32_t elapsedMs) {
        if (!activeDisplay) return;
        const int bw = screenWidth - 2 * LOAD_BAR_X;
        const int inX = LOAD_BAR_X + 4;
        const int inW = bw - 8;
        const int inY = LOAD_BAR_Y + 4;
        const int inH = LOAD_BAR_H - 8;
        const int block = 70;
        const int travel = inW - block;
        const int period = 2 * travel;

        // Clear the bar interior, then draw the block bouncing left and right
        activeDisplay->fillRect(inX, inY, inW, inH, COLOR_BG);
        int phase = (int)((elapsedMs / 5) % (uint32_t)period);
        int x = (phase <= travel) ? phase : (period - phase);
        activeDisplay->fillRect(inX + x, inY, block, inH, COLOR_YELLOW);

        // Elapsed seconds under the bar
        char buf[24];
        snprintf(buf, sizeof(buf), "Please wait  %lus", (unsigned long)(elapsedMs / 1000));
        activeDisplay->fillRect(0, LOAD_BAR_Y + LOAD_BAR_H + 10, screenWidth, 20, COLOR_BG);
        textCentered(String(buf), 0, screenWidth, LOAD_BAR_Y + LOAD_BAR_H + 12, 2, COLOR_GRAY);
    }

    // =========================================================================
    // QR: scannable code on the left, amount in the header, live countdown
    // =========================================================================
    static constexpr int QR_X = 6;
    static constexpr int QR_Y = HEADER_H + 4;
    static constexpr int QR_SIZE = 164;
    static constexpr int PANEL_X = 180;
    static constexpr int PANEL_W = 134;

    void showQRScreen(const String& qrPayload, float amount, uint32_t durationSec,
                      const String& qrId, uint32_t remainingSec) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("PAY " + formatAmount(amount), COLOR_WHITE, COLOR_BLACK);

        // White QR card with black modules (highest contrast possible)
        QRCodeRenderer::renderToDisplay(activeDisplay, qrPayload, QR_X, QR_Y, QR_SIZE,
                                        COLOR_BLACK, COLOR_WHITE);

        // Right panel: instructions, timer, provider
        drawCard(PANEL_X, QR_Y, PANEL_W, QR_SIZE, COLOR_GRAY);
        const int tx = PANEL_X + 8;
        text("SCAN WITH", tx, QR_Y + 8, 2, COLOR_WHITE);
        text("ANY UPI", tx, QR_Y + 26, 2, COLOR_WHITE);
        activeDisplay->drawFastHLine(PANEL_X + 6, QR_Y + 50, PANEL_W - 12, COLOR_DARK_GRAY);

        text("TIME LEFT", tx, QR_Y + 58, 2, COLOR_YELLOW);
        updateQRCountdown(remainingSec);

        text("Razorpay", tx, QR_Y + 118, 2, COLOR_GREEN);
        text("VERIFIED", tx, QR_Y + 138, 2, COLOR_GRAY);

        drawFooter("LEFT:CANCEL", "", "WAITING", COLOR_YELLOW);
    }

    // Redraws only the timer (no flicker). Turns red in the last 30 s, orange in the last minute.
    void updateQRCountdown(uint32_t remainingSec) {
        if (!activeDisplay) return;
        const int x = PANEL_X + 8;
        const int y = QR_Y + 78;
        activeDisplay->fillRect(x - 2, y - 2, PANEL_W - 12, textH(3) + 4, COLOR_BG);

        uint16_t colour = COLOR_WHITE;
        if (remainingSec <= 30) colour = COLOR_RED;
        else if (remainingSec <= 60) colour = COLOR_ORANGE;

        char buf[8];
        snprintf(buf, sizeof(buf), "%02lu:%02lu",
                 (unsigned long)(remainingSec / 60), (unsigned long)(remainingSec % 60));
        text(String(buf), x, y, 3, colour);
    }

    // =========================================================================
    // PAYMENT SUCCESS: amount, live relay countdown
    // =========================================================================
    void showPaymentSuccessScreen(float amount, uint32_t durationSec, uint32_t remainingRelaySec = 3) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("PAYMENT SUCCESSFUL", COLOR_EMERALD, COLOR_BLACK);

        textFit("PAID " + formatAmount(amount), 6, screenWidth - 12, HEADER_H + 10, 4, COLOR_GREEN);
        updatePaymentSuccessRelayStatus(remainingRelaySec);
        textFit("Power on for " + String(durationSec) + " seconds", 6, screenWidth - 12,
                HEADER_H + 110, 2, COLOR_YELLOW);

        drawFooter("", "RELAY ENERGIZED", "", COLOR_GREEN);
    }

    // Redraws only the relay status box (no flicker)
    void updatePaymentSuccessRelayStatus(uint32_t remainingRelaySec) {
        if (!activeDisplay) return;
        const int bx = 10, by = HEADER_H + 62, bw = screenWidth - 20, bh = 40;

        activeDisplay->fillRoundRect(bx, by, bw, bh, 6, COLOR_BG);
        drawCard(bx, by, bw, bh, COLOR_GREEN);

        String relayText = (remainingRelaySec > 0)
                               ? "RELAY ON " + String(remainingRelaySec) + "s"
                               : String("RELAY OFF");
        textFit(relayText, bx + 6, bw - 12, by + (bh - textH(3)) / 2, 3, COLOR_GREEN);
    }

    // =========================================================================
    // PAYMENT FAILED
    // =========================================================================
    void showPaymentFailedScreen(const String& reason = "Payment Expired") {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("PAYMENT FAILED", COLOR_RED, COLOR_WHITE);

        // Red X badge
        const int cx = screenWidth / 2;
        const int cy = HEADER_H + 34;
        activeDisplay->fillCircle(cx, cy, 22, COLOR_RED);
        textCentered("X", cx - 20, 40, cy - 12, 3, COLOR_WHITE);

        String shown = reason.length() > 0 ? reason : String("Timeout / Expired");
        if (shown.length() > 24) shown = shown.substring(0, 24);
        textFit(shown, 6, screenWidth - 12, HEADER_H + 66, 3, COLOR_WHITE);

        textFit("No amount was charged", 6, screenWidth - 12, HEADER_H + 102, 2, COLOR_GRAY);
        textFit("Relay stayed OFF", 6, screenWidth - 12, HEADER_H + 124, 2, COLOR_YELLOW);

        drawFooter("", "Returning to home...", "", COLOR_YELLOW);
    }
};

#endif // DISPLAY_MANAGER_H
