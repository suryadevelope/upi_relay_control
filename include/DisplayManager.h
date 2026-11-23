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
// High-contrast outdoor & arcade palette: pure black background, white / yellow /
// bright green / cyan text, crisp cards with zero text collisions.
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
#define COLOR_GRAY        0xC618  // Light grey, readable in bright light
#define COLOR_DARK_GRAY   0x7BEF  // Mid grey, used for dividers

// Text sizes (built-in font scaled by size): size 2 = 12x16 px,
// size 3 = 18x24 px, size 4 = 24x32 px, size 5 = 30x40 px.
// Minimum text size is 2 (12x16 px) for readability.

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

    String formatDuration(uint32_t sec) const {
        if (sec >= 60 && (sec % 60 == 0)) {
            return String(sec / 60) + " MIN";
        }
        return String(sec) + "s";
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

    // Two-pixel outline, card edge stays crisp in bright light
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

    void begin(const DeviceSettings& settings) {
        rotation = settings.tftRotation;
        blPin = settings.tftBL;

        if (blPin >= 0) {
            ledcSetup(BL_CHANNEL, 5000, 8);      // 5 kHz, 8-bit PWM
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

    void setBrightness(uint8_t level) {
        brightness = level;
        writeBacklight(level);
    }

    uint8_t getBrightness() const { return brightness; }

    // =========================================================================
    // 1. HOME SCREEN: Arcade game pass selection (UP / DOWN select, ENTER plays)
    // =========================================================================
    void showMenuHome(const String& deviceName, bool serviceEnabled, bool staConnected,
                      const String& staIP, int ruleCount, const PaymentRule* rules,
                      int selectedIndex) {
        if (!activeDisplay) return;
        const int w = screenWidth;

        // ---- Header: Arcade Title left, Wi-Fi and service status right ----
        activeDisplay->fillRect(0, 0, w, HEADER_H, COLOR_WHITE);

        const int pillH = 22;
        const int pillY = (HEADER_H - pillH) / 2;

        const String svc = serviceEnabled ? "ON" : "STOP";
        const int svcW = textW(svc, 2) + 14;
        const int svcX = w - 6 - svcW;
        activeDisplay->fillRoundRect(svcX, pillY, svcW, pillH, 4,
                                     serviceEnabled ? COLOR_GREEN : COLOR_RED);
        textCentered(svc, svcX, svcW, pillY + 3, 2, serviceEnabled ? COLOR_BLACK : COLOR_WHITE);

        const String net = staConnected ? "WIFI" : "OFF";
        const int netW = textW(net, 2) + 14;
        const int netX = svcX - 6 - netW;
        activeDisplay->fillRoundRect(netX, pillY, netW, pillH, 4,
                                     staConnected ? COLOR_GREEN : COLOR_RED);
        textCentered(net, netX, netW, pillY + 3, 2, staConnected ? COLOR_BLACK : COLOR_WHITE);

        const int titleMax = netX - 6 - 8;
        const uint8_t titleSize = fitSize(deviceName, titleMax, 3);
        text(truncateToWidth(deviceName, titleMax, titleSize), 8,
             (HEADER_H - textH(titleSize)) / 2, titleSize, COLOR_BLACK);

        // ---- Body: 3 arcade play cards ----
        clearBody();
        const int bodyTop = HEADER_H + 6;
        const int rowH = 46;
        const int rowGap = 6;
        const int listX = 10;
        const int listW = (ruleCount > 3) ? (w - 20 - 18) : (w - 20);
        int maxRows = 3;

        if (ruleCount <= 0 || rules == nullptr) {
            textFit("NO GAME CARDS", 6, w - 12, bodyTop + 24, 4, COLOR_YELLOW);
            textFit("Configure cards in admin app", 6, w - 12, bodyTop + 88, 2, COLOR_GRAY);
        } else {
            if (selectedIndex < 0) selectedIndex = 0;
            if (selectedIndex >= ruleCount) selectedIndex = ruleCount - 1;

            int first = selectedIndex - maxRows / 2;
            if (first > ruleCount - maxRows) first = ruleCount - maxRows;
            if (first < 0) first = 0;

            for (int row = 0; row < maxRows && (first + row) < ruleCount; row++) {
                const int idx = first + row;
                const int y = bodyTop + row * (rowH + rowGap);
                const bool sel = (idx == selectedIndex);

                if (sel) {
                    activeDisplay->fillRoundRect(listX, y, listW, rowH, 6, COLOR_YELLOW);
                } else {
                    activeDisplay->fillRoundRect(listX, y, listW, rowH, 6, COLOR_BG);
                    drawCard(listX, y, listW, rowH, COLOR_DARK_GRAY);
                }

                const String dur = "PLAY " + formatDuration(rules[idx].durationSec);
                const int durW = textW(dur, 2);
                const String amount = formatAmount(rules[idx].amount);
                const int amountMax = listW - 28 - durW - 12;
                const uint8_t amountSize = fitSize(amount, amountMax, 4);
                const uint16_t ink = sel ? COLOR_BLACK : COLOR_WHITE;

                text(amount, listX + 14, y + (rowH - textH(amountSize)) / 2, amountSize, ink);
                text(dur, listX + listW - 14 - durW, y + (rowH - textH(2)) / 2, 2,
                     sel ? COLOR_BLACK : COLOR_YELLOW);
            }

            // Scroll arrows in gutter when ruleCount > 3
            if (ruleCount > 3) {
                const int gx = w - 10;
                if (first > 0) {
                    activeDisplay->fillTriangle(gx - 5, bodyTop + 10, gx + 5, bodyTop + 10,
                                                gx, bodyTop + 2, COLOR_YELLOW);
                }
                if (first + maxRows < ruleCount) {
                    const int by = bodyTop + maxRows * (rowH + rowGap) - rowGap;
                    activeDisplay->fillTriangle(gx - 5, by - 10, gx + 5, by - 10,
                                                gx, by - 2, COLOR_YELLOW);
                }
            }
        }

        // ---- Footer ----
        if (serviceEnabled) {
            drawFooter("UP/DN", "ENTER:PLAY", "INFO", COLOR_GREEN);
        } else {
            drawFooter("UP/DN", "PAY OFF", "INFO", COLOR_RED);
        }
    }

    // =========================================================================
    // 2. CONFIRM SCREEN: Shown before QR generation
    // =========================================================================
    void showConfirmScreen(float amount, uint32_t durationSec) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("START GAME", COLOR_WHITE, COLOR_BLACK);

        const int cardX = 14;
        const int cardY = HEADER_H + 8;
        const int cardW = screenWidth - 28;
        const int cardH = screenHeight - HEADER_H - FOOTER_H - 16;

        drawCard(cardX, cardY, cardW, cardH, COLOR_YELLOW);

        textCentered("GAME TICKET", cardX, cardW, cardY + 12, 2, COLOR_GRAY);
        textFit(formatAmount(amount), cardX + 10, cardW - 20, cardY + 30, 4, COLOR_GREEN);

        activeDisplay->drawFastHLine(cardX + 16, cardY + 74, cardW - 32, COLOR_DARK_GRAY);

        textCentered("GAME TIME: " + formatDuration(durationSec), cardX, cardW, cardY + 86, 2, COLOR_YELLOW);
        textCentered("PRESS ENTER TO PLAY", cardX, cardW, cardY + 114, 2, COLOR_WHITE);

        drawFooter("LEFT:BACK", "", "ENTER:PAY", COLOR_YELLOW);
    }

    // =========================================================================
    // 3. LOADING SCREEN: Contacting backend to generate UPI QR
    // =========================================================================
    static constexpr int LOAD_CARD_X = 14;
    static constexpr int LOAD_CARD_Y = HEADER_H + 8; // 48
    static constexpr int LOAD_CARD_W = 292;
    static constexpr int LOAD_CARD_H = 154; // bottom = 202

    static constexpr int LOAD_BAR_X = 36;
    static constexpr int LOAD_BAR_Y = 138;
    static constexpr int LOAD_BAR_W = 248;
    static constexpr int LOAD_BAR_H = 20;

    void showLoadingScreen(float amount) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("CONNECTING TO BANK...", COLOR_WHITE, COLOR_BLACK);

        drawCard(LOAD_CARD_X, LOAD_CARD_Y, LOAD_CARD_W, LOAD_CARD_H, COLOR_YELLOW);

        // Row 1: Label (y = 60..76)
        textCentered("GAME PASS TICKET", LOAD_CARD_X, LOAD_CARD_W, LOAD_CARD_Y + 12, 2, COLOR_GRAY);

        // Row 2: Amount (y = 80..104)
        textFit(formatAmount(amount), LOAD_CARD_X + 10, LOAD_CARD_W - 20, LOAD_CARD_Y + 32, 3, COLOR_GREEN);

        // Row 3: Divider line (y = 108)
        activeDisplay->drawFastHLine(LOAD_CARD_X + 16, LOAD_CARD_Y + 60, LOAD_CARD_W - 32, COLOR_DARK_GRAY);

        // Row 4: Status text (y = 116..132, 19 chars = 228px, centered in 292px)
        textCentered("Creating Game QR...", LOAD_CARD_X, LOAD_CARD_W, LOAD_CARD_Y + 68, 2, COLOR_YELLOW);

        // Row 5: Progress bar outline (y = 138..158)
        drawCard(LOAD_BAR_X, LOAD_BAR_Y, LOAD_BAR_W, LOAD_BAR_H, COLOR_WHITE);
        updateLoadingBar(0);

        // Footer: (y = 210..240)
        drawFooter("LEFT:CANCEL", "", "", COLOR_YELLOW);
    }

    void updateLoadingBar(uint32_t elapsedMs) {
        if (!activeDisplay) return;
        const int inX = LOAD_BAR_X + 3;
        const int inW = LOAD_BAR_W - 6;
        const int inY = LOAD_BAR_Y + 3;
        const int inH = LOAD_BAR_H - 6;
        const int block = 60;
        const int travel = inW - block;
        const int period = 2 * travel;

        activeDisplay->fillRect(inX, inY, inW, inH, COLOR_BG);
        int phase = (int)((elapsedMs / 5) % (uint32_t)period);
        int x = (phase <= travel) ? phase : (period - phase);
        activeDisplay->fillRect(inX + x, inY, block, inH, COLOR_YELLOW);

        // Row 6: Elapsed seconds under progress bar (y = 166..182)
        char buf[32];
        snprintf(buf, sizeof(buf), "Please wait...  %lus", (unsigned long)(elapsedMs / 1000));
        activeDisplay->fillRect(LOAD_CARD_X + 20, LOAD_BAR_Y + LOAD_BAR_H + 8, LOAD_CARD_W - 40, 18, COLOR_BG);
        textCentered(String(buf), LOAD_CARD_X, LOAD_CARD_W, LOAD_BAR_Y + LOAD_BAR_H + 8, 2, COLOR_GRAY);
    }

    // =========================================================================
    // 4. QR SCREEN: High-contrast QR code with timer and game badges
    // =========================================================================
    static constexpr int QR_X = 8;
    static constexpr int QR_Y = HEADER_H + 4; // 44
    static constexpr int QR_SIZE = 158;
    static constexpr int PANEL_X = 174;
    static constexpr int PANEL_W = 138;

    void showQRScreen(const String& qrPayload, float amount, uint32_t durationSec,
                      const String& qrId, uint32_t remainingSec) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("SCAN TO PLAY: " + formatAmount(amount), COLOR_WHITE, COLOR_BLACK);

        // White background card with black QR modules for optimal camera scanning
        QRCodeRenderer::renderToDisplay(activeDisplay, qrPayload, QR_X, QR_Y, QR_SIZE,
                                        COLOR_BLACK, COLOR_WHITE);

        // Right panel outline
        drawCard(PANEL_X, QR_Y, PANEL_W, QR_SIZE, COLOR_GRAY);

        // 1. Top badge: "PLAY GAME"
        activeDisplay->fillRoundRect(PANEL_X + 6, QR_Y + 6, PANEL_W - 12, 22, 4, COLOR_YELLOW);
        textCentered("PLAY GAME", PANEL_X + 6, PANEL_W - 12, QR_Y + 9, 2, COLOR_BLACK);

        // 2. Subtitle: "ANY UPI APP"
        textCentered("ANY UPI APP", PANEL_X, PANEL_W, QR_Y + 32, 2, COLOR_WHITE);

        // 3. Divider
        activeDisplay->drawFastHLine(PANEL_X + 8, QR_Y + 52, PANEL_W - 16, COLOR_DARK_GRAY);

        // 4. Timer section
        textCentered("TIME LEFT", PANEL_X, PANEL_W, QR_Y + 58, 2, COLOR_YELLOW);
        updateQRCountdown(remainingSec);

        // 5. Divider
        activeDisplay->drawFastHLine(PANEL_X + 8, QR_Y + 110, PANEL_W - 16, COLOR_DARK_GRAY);

        // 6. Game time and auto start badge
        textCentered("TIME: " + formatDuration(durationSec), PANEL_X, PANEL_W, QR_Y + 116, 2, COLOR_GREEN);
        textCentered("AUTO START", PANEL_X, PANEL_W, QR_Y + 134, 2, COLOR_CYAN);

        drawFooter("LEFT:CANCEL", "", "ENTER:CANCEL", COLOR_YELLOW);
    }

    void updateQRCountdown(uint32_t remainingSec) {
        if (!activeDisplay) return;
        const int y = QR_Y + 80;
        activeDisplay->fillRect(PANEL_X + 4, y - 2, PANEL_W - 8, textH(3) + 4, COLOR_BG);

        uint16_t colour = COLOR_WHITE;
        if (remainingSec <= 30) colour = COLOR_RED;
        else if (remainingSec <= 60) colour = COLOR_ORANGE;

        char buf[8];
        snprintf(buf, sizeof(buf), "%02lu:%02lu",
                 (unsigned long)(remainingSec / 60), (unsigned long)(remainingSec % 60));
        textCentered(String(buf), PANEL_X, PANEL_W, y, 3, colour);
    }

    // =========================================================================
    // 5. PAYMENT SUCCESS SCREEN: Game active, relay energized, countdown to 0
    // =========================================================================
    void showPaymentSuccessScreen(float amount, uint32_t durationSec, uint32_t remainingRelaySec = 3) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("GAME STARTED! HAVE FUN", COLOR_EMERALD, COLOR_BLACK);

        const int cardX = 14;
        const int cardY = HEADER_H + 8;
        const int cardW = screenWidth - 28;
        const int cardH = screenHeight - HEADER_H - FOOTER_H - 16;

        drawCard(cardX, cardY, cardW, cardH, COLOR_EMERALD);

        textCentered("PAYMENT VERIFIED", cardX, cardW, cardY + 10, 2, COLOR_WHITE);
        textFit(formatAmount(amount) + " COIN INSERTED", cardX + 10, cardW - 20, cardY + 30, 3, COLOR_YELLOW);

        updatePaymentSuccessRelayStatus(remainingRelaySec);

        textCentered("PLAY TIME: " + formatDuration(durationSec), cardX, cardW, cardY + 110, 2, COLOR_WHITE);
        textCentered("CONTROLS ENERGIZED", cardX, cardW, cardY + 130, 2, COLOR_YELLOW);

        drawFooter("FUNNHUB", "GAME ACTIVE", "PLAY ON!", COLOR_GREEN);
    }

    void updatePaymentSuccessRelayStatus(uint32_t remainingRelaySec) {
        if (!activeDisplay) return;
        const int cardX = 14;
        const int cardY = HEADER_H + 8;
        const int cardW = screenWidth - 28;
        const int bx = cardX + 16;
        const int by = cardY + 56;
        const int bw = cardW - 32;
        const int bh = 46;

        activeDisplay->fillRoundRect(bx, by, bw, bh, 6, COLOR_BG);

        if (remainingRelaySec > 0) {
            drawCard(bx, by, bw, bh, COLOR_GREEN);
            String playText = "GAME ON: " + String(remainingRelaySec) + "s";
            textFit(playText, bx + 6, bw - 12, by + (bh - textH(3)) / 2, 3, COLOR_GREEN);
        } else {
            drawCard(bx, by, bw, bh, COLOR_RED);
            textFit("GAME OVER", bx + 6, bw - 12, by + (bh - textH(3)) / 2, 3, COLOR_RED);
        }
    }

    // =========================================================================
    // 5b. MODE 2 SUCCESS SCREEN: fixed 5s relay + "enjoy the game" (device only)
    // =========================================================================
    void showMode2SuccessScreen(float amount) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("MODE 2 - 5S PLAY", COLOR_EMERALD, COLOR_BLACK);

        const int cardX = 14;
        const int cardY = HEADER_H + 8;
        const int cardW = screenWidth - 28;
        const int cardH = screenHeight - HEADER_H - FOOTER_H - 16;

        drawCard(cardX, cardY, cardW, cardH, COLOR_EMERALD);

        textCentered("PAYMENT VERIFIED", cardX, cardW, cardY + 10, 2, COLOR_WHITE);
        textFit(formatAmount(amount) + " COIN INSERTED", cardX + 10, cardW - 20, cardY + 30, 3, COLOR_YELLOW);

        // Big device-only message
        textFit("ENJOY THE GAME", cardX + 10, cardW - 20, cardY + 58, 3, COLOR_GREEN);

        updateMode2RelayStatus(MODE2_RELAY_SECONDS);

        textCentered("RELAY ON FOR 5s", cardX, cardW, cardY + 116, 2, COLOR_YELLOW);
        textCentered("AUTO OFF WHEN TIME ENDS", cardX, cardW, cardY + 134, 2, COLOR_CYAN);

        drawFooter("FUNNHUB", "MODE 2 ACTIVE", "ENJOY!", COLOR_GREEN);
    }

    void updateMode2RelayStatus(uint32_t remainingRelaySec) {
        if (!activeDisplay) return;
        const int cardX = 14;
        const int cardY = HEADER_H + 8;
        const int cardW = screenWidth - 28;
        const int bx = cardX + 16;
        const int by = cardY + 90;
        const int bw = cardW - 32;
        const int bh = 46;

        activeDisplay->fillRoundRect(bx, by, bw, bh, 6, COLOR_BG);

        if (remainingRelaySec > 0) {
            drawCard(bx, by, bw, bh, COLOR_GREEN);
            String playText = "ENJOY THE GAME: " + String(remainingRelaySec) + "s";
            textFit(playText, bx + 6, bw - 12, by + (bh - textH(3)) / 2, 3, COLOR_GREEN);
        } else {
            drawCard(bx, by, bw, bh, COLOR_RED);
            textFit("ENJOY YOUR GAME", bx + 6, bw - 12, by + (bh - textH(3)) / 2, 3, COLOR_RED);
        }
    }

    // =========================================================================
    // 6. PAYMENT FAILED / CANCELLED SCREEN
    // =========================================================================
    void showPaymentFailedScreen(const String& reason = "Payment Expired") {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("GAME CANCELLED", COLOR_RED, COLOR_WHITE);

        const int cardX = 14;
        const int cardY = HEADER_H + 8;
        const int cardW = screenWidth - 28;
        const int cardH = screenHeight - HEADER_H - FOOTER_H - 16;

        drawCard(cardX, cardY, cardW, cardH, COLOR_RED);

        const int cx = screenWidth / 2;
        const int cy = cardY + 24;
        activeDisplay->fillCircle(cx, cy, 16, COLOR_RED);
        textCentered("X", cx - 16, 32, cy - 8, 2, COLOR_WHITE);

        String shown = reason.length() > 0 ? reason : String("Timeout / Cancelled");
        if (shown.length() > 22) shown = shown.substring(0, 22);
        textFit(shown, cardX + 10, cardW - 20, cardY + 48, 3, COLOR_WHITE);

        activeDisplay->drawFastHLine(cardX + 16, cardY + 78, cardW - 32, COLOR_DARK_GRAY);

        textCentered("No money was charged", cardX, cardW, cardY + 90, 2, COLOR_GRAY);
        textCentered("Game relay remained OFF", cardX, cardW, cardY + 114, 2, COLOR_YELLOW);

        drawFooter("", "RETURNING TO MENU...", "", COLOR_YELLOW);
    }

    // =========================================================================
    // 7. INFO SCREEN: Machine status and identity
    // =========================================================================
    void showInfoScreen(const String& deviceName, const String& fwVersion, const String& apSSID,
                        bool staConnected, const String& staIP, bool serviceEnabled, bool relayOn) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        drawHeader("ARCADE MACHINE INFO", COLOR_WHITE, COLOR_BLACK);

        int y = HEADER_H + 10;
        const int step = 25;
        const int valueX = 110;
        const int valueMax = screenWidth - valueX - 8;
        auto row = [&](const char* label, const String& value, uint16_t colour) {
            text(label, 10, y, 2, COLOR_GRAY);
            text(truncateToWidth(value, valueMax, 2), valueX, y, 2, colour);
            y += step;
        };

        row("Machine", deviceName, COLOR_WHITE);
        row("Firmware", fwVersion, COLOR_WHITE);
        row("Wi-Fi", (staConnected && staIP.length() > 0) ? staIP : String("offline"),
            staConnected ? COLOR_GREEN : COLOR_RED);
        row("Hotspot", apSSID, COLOR_CYAN);
        row("Status", serviceEnabled ? String("READY TO PLAY") : String("MAINTENANCE"),
            serviceEnabled ? COLOR_GREEN : COLOR_RED);
        row("Relay", relayOn ? String("ON (PLAYING)") : String("IDLE (OFF)"),
            relayOn ? COLOR_YELLOW : COLOR_GRAY);

        drawFooter("LEFT:BACK", "", "", COLOR_YELLOW);
    }

    // =========================================================================
    // 8. NOTICE SCREEN: Popup message that auto-closes
    // =========================================================================
    void showNoticeScreen(const String& title, const String& message, uint16_t colour) {
        if (!activeDisplay) return;
        activeDisplay->fillScreen(COLOR_BG);
        const uint16_t ink = (colour == COLOR_RED) ? COLOR_WHITE : COLOR_BLACK;
        drawHeader(title, colour, ink);

        const int cardX = 14;
        const int cardY = HEADER_H + 16;
        const int cardW = screenWidth - 28;
        const int cardH = screenHeight - HEADER_H - FOOTER_H - 32;
        drawCard(cardX, cardY, cardW, cardH, colour);

        textFit(message, cardX + 10, cardW - 20, cardY + (cardH - textH(3)) / 2, 3, COLOR_WHITE);
        drawFooter("", "", "", COLOR_BG);
    }
};

#endif // DISPLAY_MANAGER_H