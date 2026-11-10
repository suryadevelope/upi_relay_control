#ifndef SECURITY_UTILS_H
#define SECURITY_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>
#include "mbedtls/md.h"

namespace SecurityUtils {

// Lower-case hex encoding of a byte buffer
inline String toHex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += digits[(data[i] >> 4) & 0x0F];
        out += digits[data[i] & 0x0F];
    }
    return out;
}

// Cryptographically random hex string built from the ESP32 hardware RNG
inline String randomHex(size_t bytes) {
    uint8_t buf[32];
    if (bytes > sizeof(buf)) bytes = sizeof(buf);
    esp_fill_random(buf, bytes);
    return toHex(buf, bytes);
}

// HMAC-SHA256(key, message) as lower-case hex
inline String hmacSha256Hex(const String& key, const String& message) {
    uint8_t out[32];
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info,
                    (const unsigned char*)key.c_str(), key.length(),
                    (const unsigned char*)message.c_str(), message.length(),
                    out);
    return toHex(out, sizeof(out));
}

// Length-independent comparison so the proof check does not leak timing
inline bool constantTimeEquals(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    uint8_t diff = 0;
    for (unsigned int i = 0; i < a.length(); i++) {
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    }
    return diff == 0;
}

// True only when the HTTP request arrived over the device's own hotspot (SoftAP).
// Used to keep Wi-Fi credential changes off the LAN.
inline bool requestFromHotspot(WebServer& server) {
    IPAddress local = server.client().localIP();
    return local == WiFi.softAPIP();
}

} // namespace SecurityUtils

#endif // SECURITY_UTILS_H
