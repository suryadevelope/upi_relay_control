#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==========================================
// System Hardware Pin Assignments (Default)
// ==========================================

// 5-button menu navigation (all Active LOW, internal pull-ups enabled in firmware)
#define PIN_BTN_ENTER              32   // Select / confirm / pay
#define PIN_BTN_DOWN               13   // Next amount card (was 27: false GND)
#define PIN_BTN_UP                 25   // Previous amount card
#define PIN_BTN_RIGHT              16   // Info screen / next page (14 read LOW at idle: false GND)
#define PIN_BTN_LEFT               33   // Back / cancel

// Relay Pin
#define DEFAULT_RELAY_PIN          26
#define DEFAULT_RELAY_ACTIVE_HIGH  true

// KMRTM24024-SPI 2.8-inch TFT (ST7789, 240x320 native, 320x240 landscape)
// Pins must match the TFT_* build_flags in platformio.ini (TFT_eSPI is configured there).
// ESP32 VSPI default pins: MOSI = GPIO 23, SCLK = GPIO 18, MISO = GPIO 19
#define DEFAULT_TFT_MOSI           23
#define DEFAULT_TFT_SCLK           18
#define DEFAULT_TFT_MISO           19
#define DEFAULT_TFT_CS             5
#define DEFAULT_TFT_DC             21
#define DEFAULT_TFT_RST            22
#define DEFAULT_TFT_BL             4   // Backlight LED (optional)

// Display Driver Types (driver is fixed at build time by ST7789_DRIVER in platformio.ini)
enum DisplayDriverType {
    DISPLAY_ST7789 = 0,
    DISPLAY_ILI9341 = 1
};
#define DEFAULT_DISPLAY_TYPE       DISPLAY_ST7789

// ==========================================
// Default Network & Security Credentials
// ==========================================
#define DEFAULT_AP_SSID            "SmartGateway-Setup"
#define DEFAULT_AP_PASS            "gateway1234"
#define DEFAULT_AP_IP              IPAddress(192, 168, 4, 1)

#define DEFAULT_ADMIN_USER         "admin"
#define DEFAULT_ADMIN_PASS         "admin123"

// Device identity and local network services
#define DEFAULT_DEVICE_NAME        "FUNNHUB ARCADE"
#define FIRMWARE_VERSION           "2.0.0"
#define SETUP_HTTP_PORT            80     // Hotspot-only Wi-Fi setup page (never served on the LAN)
#define ADMIN_API_PORT             8080   // JSON admin API for the desktop app (LAN + hotspot)
#define DISCOVERY_UDP_PORT         4210   // UDP broadcast discovery for the desktop app
#define ADMIN_SESSION_IDLE_MS      (15UL * 60UL * 1000UL)
#define ADMIN_LOGIN_MAX_FAILS      5
#define ADMIN_LOGIN_LOCK_MS        (30UL * 1000UL)

// Default Backend URL and Device Key
// Cloud backend deployed on Railway (backend/ folder). HTTPS is handled by PaymentClient.
// For local testing, point this to the PC running backend/src/server.js (e.g. http://<LAN-IP>:3000).
#define DEFAULT_BACKEND_URL        "https://upirelay.up.railway.app"
#define DEFAULT_DEVICE_KEY         "sg_device_secret_key_123"

// Default Debug Mode (QR code is rendered on TFT; serial QR printing disabled)
#define DEFAULT_DEBUG_MODE         false

// Maximum amount-to-duration rules
#define MAX_RULES                  10
#define MAX_TRANSACTIONS_LOGGED    15

// QR & Polling timeouts
#define QR_EXPIRY_SECONDS          300      // 5 minutes
#define STATUS_POLL_INTERVAL_MS    1500     // Poll backend every 1.5s (fast confirmation, optimal load for 15-30 devices)

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
    char deviceName[33];
    bool serviceEnabled;   // false = admin stopped the service: no payments, relay forced OFF
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
