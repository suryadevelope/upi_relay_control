#ifndef QR_CODE_RENDERER_H
#define QR_CODE_RENDERER_H

#include <Arduino.h>
#include <qrcode.h>   // ESP-IDF built-in QR encoder (esp_qrcode_*)
#include <TFT_eSPI.h>

// Max QR version to allow (Version 15 ECC LOW holds ~520 bytes, plenty for UPI payloads)
#define QR_MAX_VERSION 15

class QRCodeRenderer {
private:
    struct DisplayCtx {
        TFT_eSPI* display;
        int cardX;
        int cardY;
        int cardSize;
        uint16_t fgColor;
        uint16_t bgColor;
    };

    static DisplayCtx& displayCtx() {
        static DisplayCtx ctx;
        return ctx;
    }

    static void drawToDisplay(esp_qrcode_handle_t qr) {
        DisplayCtx& c = displayCtx();
        if (!c.display) return;

        int qrSize = esp_qrcode_get_size(qr); // Number of modules across (e.g. 33, 37, 41)
        if (qrSize <= 0) return;

        // Calculate maximum integer module scale that fits inside card with quiet zone margin
        // Ensure at least 8px margin (quiet zone) on all sides
        int maxUsable = c.cardSize - 16;
        int scale = maxUsable / qrSize;
        if (scale < 1) scale = 1;
        if (scale > 5) scale = 5;

        int actualQrPixelWidth = qrSize * scale;
        // Center the QR symbol exactly inside the white card
        int startX = c.cardX + (c.cardSize - actualQrPixelWidth) / 2;
        int startY = c.cardY + (c.cardSize - actualQrPixelWidth) / 2;

        // 1. Draw solid, high-contrast white card background with rounded corners
        c.display->fillRoundRect(c.cardX, c.cardY, c.cardSize, c.cardSize, 6, c.bgColor);
        c.display->drawRoundRect(c.cardX, c.cardY, c.cardSize, c.cardSize, 6, 0x07FF); // Cyan border

        // 2. Render black QR modules with horizontal run-length batching for fast SPI transfer
        for (int y = 0; y < qrSize; y++) {
            int py = startY + (y * scale);
            int x = 0;
            while (x < qrSize) {
                if (esp_qrcode_get_module(qr, x, y)) {
                    int runStart = x;
                    while (x < qrSize && esp_qrcode_get_module(qr, x, y)) {
                        x++;
                    }
                    int runLen = x - runStart;
                    int px = startX + (runStart * scale);
                    c.display->fillRect(px, py, runLen * scale, scale, c.fgColor);
                } else {
                    x++;
                }
            }
        }
    }

    static esp_err_t generate(void (*displayFunc)(esp_qrcode_handle_t), const String& text) {
        esp_qrcode_config_t config;
        config.display_func = displayFunc;
        config.max_qrcode_version = QR_MAX_VERSION;
        config.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
        return esp_qrcode_generate(&config, text.c_str());
    }

public:
    // Render QR Code onto the TFT_eSPI display inside a high-contrast white card
    static bool renderToDisplay(TFT_eSPI* display, const String& text,
                                int cardX, int cardY, int cardSize,
                                uint16_t fgColor = 0x0000, uint16_t bgColor = 0xFFFF) {
        if (!display || text.length() == 0) return false;

        displayCtx() = { display, cardX, cardY, cardSize, fgColor, bgColor };
        esp_err_t result = generate(drawToDisplay, text);
        if (result != ESP_OK) {
            Serial.printf("[QR] Failed to generate QR code for length %d (err: %d)\n",
                          text.length(), (int)result);
            return false;
        }
        return true;
    }

    // Serial monitor QR code printing is disabled per user request
    static void printToSerial(const String& text, float amount, uint32_t durationSec, const String& qrId) {
        // Disabled: Serial Monitor QR printing disabled. QR code is displayed on the 2.8" TFT display.
    }
};

#endif // QR_CODE_RENDERER_H
