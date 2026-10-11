#ifndef QR_CODE_RENDERER_H
#define QR_CODE_RENDERER_H

#include <Arduino.h>
#include <qrcode.h>   // ESP-IDF built-in QR encoder (esp_qrcode_*)
#include <Adafruit_GFX.h>

// Max QR version to allow. Version 15 at ECC LOW holds ~520 bytes, which is
// plenty of headroom for UPI payloads; the encoder picks the smallest fit.
#define QR_MAX_VERSION 15

// Serial terminals are usually light text on a dark background, so a "filled"
// block shows up light. Inverting draws light modules as blocks, giving dark
// modules on a light field - the polarity phone scanners expect.
// Set to false if your terminal has a light background.
#define SERIAL_QR_INVERT true

class QRCodeRenderer {
private:
    // Each pattern represents a 2x2 group of QR modules.
    // Bit 0 = top-left, bit 1 = top-right,
    // bit 2 = bottom-left, bit 3 = bottom-right.
    static const char* pattern(uint8_t p) {
        static const char* patterns[16] = {
            "  ",
            "▀ ",
            " ▀",
            "▀▀",
            "▄ ",
            "█ ",
            "▄▀",
            "█▀",
            " ▄",
            "▀▄",
            " █",
            "▀█",
            "▄▄",
            "█▄",
            "▄█",
            "██"
        };
        return patterns[p & 0x0F];
    }

    // esp_qrcode_get_module() returns false outside the symbol, so the border
    // is treated as light modules automatically.
    static bool filled(esp_qrcode_handle_t qr, int x, int y) {
        bool dark = esp_qrcode_get_module(qr, x, y);
        return SERIAL_QR_INVERT ? !dark : dark;
    }

    static void printQR(esp_qrcode_handle_t qr) {
        int size = esp_qrcode_get_size(qr);
        const int border = 4;

        Serial.println();
        for (int y = -border; y < size + border; y += 2) {
            for (int x = -border; x < size + border; x += 2) {
                uint8_t p = 0;
                if (filled(qr, x, y))         p |= 1;
                if (filled(qr, x + 1, y))     p |= 2;
                if (filled(qr, x, y + 1))     p |= 4;
                if (filled(qr, x + 1, y + 1)) p |= 8;
                Serial.print(pattern(p));
            }
            Serial.println();
        }
        Serial.println();
    }

    // esp_qrcode display callbacks take no user pointer, so the TFT target is
    // passed through these statics for the duration of one generate call.
    struct DisplayCtx {
        Adafruit_GFX* display;
        int centerX, centerY, maxPixelSize;
        uint16_t fgColor, bgColor;
    };
    static DisplayCtx& displayCtx() {
        static DisplayCtx ctx;
        return ctx;
    }

    static void drawToDisplay(esp_qrcode_handle_t qr) {
        DisplayCtx& c = displayCtx();
        int qrSize = esp_qrcode_get_size(qr);
        int quietZone = 2; // 2 modules quiet zone
        int totalModules = qrSize + (quietZone * 2);

        int scale = c.maxPixelSize / totalModules;
        if (scale < 1) scale = 1;

        int actualWidth = totalModules * scale;
        int startX = c.centerX - (actualWidth / 2);
        int startY = c.centerY - (actualWidth / 2);

        // Draw quiet zone background
        c.display->fillRect(startX, startY, actualWidth, actualWidth, c.bgColor);

        // Render QR modules
        for (int y = 0; y < qrSize; y++) {
            for (int x = 0; x < qrSize; x++) {
                if (esp_qrcode_get_module(qr, x, y)) {
                    int px = startX + ((x + quietZone) * scale);
                    int py = startY + ((y + quietZone) * scale);
                    c.display->fillRect(px, py, scale, scale, c.fgColor);
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
    // Render QR Code onto Adafruit GFX display
    static bool renderToDisplay(Adafruit_GFX* display, const String& text,
                                int centerX, int centerY, int maxPixelSize,
                                uint16_t fgColor, uint16_t bgColor) {
        if (!display || text.length() == 0) return false;

        displayCtx() = { display, centerX, centerY, maxPixelSize, fgColor, bgColor };
        esp_err_t result = generate(drawToDisplay, text);
        if (result != ESP_OK) {
            Serial.printf("[QR] Failed to generate QR code for length %d (err: %d)\n",
                          text.length(), (int)result);
            return false;
        }
        return true;
    }

    // Print QR Code to Serial Monitor for Debugging & Screen Scanning
    static void printToSerial(const String& text, float amount, uint32_t durationSec, const String& qrId) {
        Serial.println();
        Serial.println("========================================================================");
        Serial.println(" [DEBUG MODE] SMART PAYMENT GATEWAY - SERIAL QR CODE");
        Serial.println("========================================================================");
        Serial.printf(" QR ID:        %s\n", qrId.length() > 0 ? qrId.c_str() : "N/A");
        Serial.printf(" Amount:       Rs. %.2f (Paise: %d)\n", amount, (int)(amount * 100));
        Serial.printf(" Duration:     %u Seconds\n", durationSec);
        Serial.printf(" Payload URL:  %s\n", text.c_str());
        Serial.println("------------------------------------------------------------------------");
        Serial.println(" SCAN QR CODE DIRECTLY FROM TERMINAL SCREEN (Google Pay / PhonePe / Paytm):");
        Serial.println("------------------------------------------------------------------------");

        esp_err_t result = generate(printQR, text);
        if (result != ESP_OK) {
            Serial.printf("[DEBUG-QR] QR generation failed: %d\n", (int)result);
        }
        Serial.println("========================================================================\n");
    }
};

#endif // QR_CODE_RENDERER_H
