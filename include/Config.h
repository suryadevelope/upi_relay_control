#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==========================================
// System Hardware Pin Assignments (Default)
// ==========================================

// Physical Start / Payment Trigger Pin (Active LOW)
// When pin D25 goes LOW, a new payment is initiated similar to webpage start button
#define DEFAULT_PAYMENT_TRIGGER_PIN 25

// Relay Pin
#define DEFAULT_RELAY_PIN          26
#define DEFAULT_RELAY_ACTIVE_HIGH  true

// 2.8-inch SPI Touch Screen Module TFT Display Pins (240x320)
// Standard ESP32 VSPI default pins:
// MOSI = GPIO 23, SCLK = GPIO 18, MISO = GPIO 19
#define DEFAULT_TFT_MOSI           23
#define DEFAULT_TFT_SCLK           18
#define DEFAULT_TFT_MISO           19
#define DEFAULT_TFT_CS             5
#define DEFAULT_TFT_DC             21
#define DEFAULT_TFT_RST            22
#define DEFAULT_TFT_BL             4   // Backlight LED (optional)

// Display Driver Types
enum DisplayDriverType {
    DISPLAY_ST7789 = 0,
    DISPLAY_ILI9341 = 1
};
#define DEFAULT_DISPLAY_TYPE       DISPLAY_ILI9341

// ==========================================
// Default Network & Security Credentials
// ==========================================
#define DEFAULT_AP_SSID            "SmartGateway-Setup"
#define DEFAULT_AP_PASS            "gateway1234"
#define DEFAULT_AP_IP              IPAddress(192, 168, 4, 1)

#define DEFAULT_ADMIN_USER         "admin"
#define DEFAULT_ADMIN_PASS         "admin123"

// Default Backend URL and Device Key
#define DEFAULT_BACKEND_URL        "http://10.13.137.189:3000"
#define DEFAULT_DEVICE_KEY         "sg_device_secret_key_123"

// Default Debug Mode (QR code is rendered on TFT; serial QR printing disabled)
#define DEFAULT_DEBUG_MODE         false

// Maximum amount-to-duration rules
#define MAX_RULES                  10
#define MAX_TRANSACTIONS_LOGGED    15

// QR & Polling timeouts
#define QR_EXPIRY_SECONDS          300      // 5 minutes
#define STATUS_POLL_INTERVAL_MS    2500     // Poll backend every 2.5s

// ==========================================
// System State Machine Definition
// ==========================================
enum GatewayState {
    STATE_STARTUP,      // Relay OFF, initializing display & Wi-Fi
    STATE_READY,        // Awaiting admin or user payment request
    STATE_QR_ACTIVE,    // Showing QR on display & awaiting payment
    STATE_VERIFYING,    // Checking captured state with cloud backend
    STATE_PAID,         // Payment captured and authorized
    STATE_RELAY_ON,     // Relay activated with non-blocking timer
    STATE_COMPLETE,     // Relay turned off, logged, returning to ready
    STATE_ERROR         // Error encountered, relay remains OFF
};

inline const char* stateToString(GatewayState s) {
    switch (s) {
        case STATE_STARTUP:   return "STARTUP";
        case STATE_READY:     return "READY";
        case STATE_QR_ACTIVE: return "QR_ACTIVE";
        case STATE_VERIFYING: return "VERIFYING";
        case STATE_PAID:      return "PAID";
        case STATE_RELAY_ON:  return "RELAY_ON";
        case STATE_COMPLETE:  return "COMPLETE";
        case STATE_ERROR:     return "ERROR";
        default:              return "UNKNOWN";
    }
}

// ==========================================
// Data Structures
// ==========================================
struct PaymentRule {
    float amount;          // Amount in Rupees (e.g., 20.0)
    uint32_t durationSec;  // Relay active duration in seconds (e.g., 30)
};

struct TransactionRecord {
    char qrId[48];
    char paymentId[48];
    float amount;
    uint32_t durationSec;
    uint32_t timestampSec;
    bool success;
};

struct DeviceSettings {
    char apSSID[33];
    char apPass[65];
    char staSSID[33];
    char staPass[65];
    char adminUser[33];
    char adminPass[33];
    char backendUrl[128];
    char deviceKey[65];
    int relayPin;
    bool relayActiveHigh;
    bool debugMode;
    int displayType;
    int tftCS;
    int tftDC;
    int tftRST;
    int tftBL;
    int tftRotation;
    int ruleCount;
    PaymentRule rules[MAX_RULES];
};

#endif // CONFIG_H
