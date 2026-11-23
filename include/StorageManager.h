#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class StorageManager {
private:
    Preferences prefs;
    DeviceSettings settings;
    TransactionRecord transactions[MAX_TRANSACTIONS_LOGGED];
    int transactionCount = 0;

public:
    StorageManager() {}

    void begin() {
        prefs.begin("smart_gw", false);
        loadSettings();
        loadTransactions();
    }

    void loadDefaultSettings() {
        strncpy(settings.deviceName, DEFAULT_DEVICE_NAME, sizeof(settings.deviceName) - 1);
        settings.serviceEnabled = true;
        strncpy(settings.apSSID, DEFAULT_AP_SSID, sizeof(settings.apSSID) - 1);
        strncpy(settings.apPass, DEFAULT_AP_PASS, sizeof(settings.apPass) - 1);
        settings.staSSID[0] = '\0';
        settings.staPass[0] = '\0';
        strncpy(settings.adminUser, DEFAULT_ADMIN_USER, sizeof(settings.adminUser) - 1);
        strncpy(settings.adminPass, DEFAULT_ADMIN_PASS, sizeof(settings.adminPass) - 1);
        strncpy(settings.backendUrl, DEFAULT_BACKEND_URL, sizeof(settings.backendUrl) - 1);
        strncpy(settings.deviceKey, DEFAULT_DEVICE_KEY, sizeof(settings.deviceKey) - 1);

        settings.relayPin = DEFAULT_RELAY_PIN;
        settings.relayActiveHigh = DEFAULT_RELAY_ACTIVE_HIGH;
        settings.debugMode = DEFAULT_DEBUG_MODE;
        settings.mode2Enabled = false;

        settings.displayType = DEFAULT_DISPLAY_TYPE; // ST7789 for the KMRTM24024-SPI 2.8" TFT
        settings.tftCS = DEFAULT_TFT_CS;
        settings.tftDC = DEFAULT_TFT_DC;
        settings.tftRST = DEFAULT_TFT_RST;
        settings.tftBL = DEFAULT_TFT_BL;
        settings.tftRotation = 1; // Landscape

        // Default Payment Rules: Game play duration
        settings.ruleCount = 3;
        {
            PaymentRule r = { 1.0f, 10, RELAY_MODE_TIMER, 1, DEFAULT_PULSE_ON_SEC, DEFAULT_PULSE_OFF_SEC, DEFAULT_PULSE_WAIT_SEC, "Best of luck for the game" };
            settings.rules[0] = r;  // Rs. 1 -> 10 Seconds Play
        }
        {
            PaymentRule r = { 5.0f, 60, RELAY_MODE_TIMER, 1, DEFAULT_PULSE_ON_SEC, DEFAULT_PULSE_OFF_SEC, DEFAULT_PULSE_WAIT_SEC, "Best of luck for the game" };
            settings.rules[1] = r;  // Rs. 5 -> 1 Minute Play
        }
        {
            PaymentRule r = { 10.0f, 180, RELAY_MODE_TIMER, 1, DEFAULT_PULSE_ON_SEC, DEFAULT_PULSE_OFF_SEC, DEFAULT_PULSE_WAIT_SEC, "Best of luck for the game" };
            settings.rules[2] = r; // Rs. 10 -> 3 Minutes Play
        }
    }

    void loadSettings() {
        if (!prefs.isKey("initialized")) {
            Serial.println("[STORAGE] First boot detected. Loading factory defaults.");
            loadDefaultSettings();
            saveSettings();
            prefs.putBool("initialized", true);
            return;
        }

        prefs.getString("deviceName", settings.deviceName, sizeof(settings.deviceName));
        if (settings.deviceName[0] == '\0' || strcmp(settings.deviceName, "Smart Gateway") == 0) {
            strncpy(settings.deviceName, DEFAULT_DEVICE_NAME, sizeof(settings.deviceName) - 1);
            prefs.putString("deviceName", settings.deviceName);
        }
        settings.serviceEnabled = prefs.getBool("svcEnabled", true);
        prefs.getString("apSSID", settings.apSSID, sizeof(settings.apSSID));
        prefs.getString("apPass", settings.apPass, sizeof(settings.apPass));
        prefs.getString("staSSID", settings.staSSID, sizeof(settings.staSSID));
        prefs.getString("staPass", settings.staPass, sizeof(settings.staPass));
        prefs.getString("adminUser", settings.adminUser, sizeof(settings.adminUser));
        prefs.getString("adminPass", settings.adminPass, sizeof(settings.adminPass));
        prefs.getString("backendUrl", settings.backendUrl, sizeof(settings.backendUrl));
        prefs.getString("deviceKey", settings.deviceKey, sizeof(settings.deviceKey));

        // Auto-migration: if NVS stores old local IP (e.g. 10.51.153.189) or is empty, or on schema upgrade,
        // point to the current cloud DEFAULT_BACKEND_URL
        int cfgVer = prefs.getInt("cfg_ver", 0);
        if (cfgVer < 2 || strstr(settings.backendUrl, "10.51.153.189") != nullptr || settings.backendUrl[0] == '\0') {
            Serial.printf("[STORAGE] Migrating backend URL '%s' -> '%s'\n", settings.backendUrl, DEFAULT_BACKEND_URL);
            strncpy(settings.backendUrl, DEFAULT_BACKEND_URL, sizeof(settings.backendUrl) - 1);
            prefs.putString("backendUrl", settings.backendUrl);
            prefs.putInt("cfg_ver", 2);
        }

        settings.relayPin = prefs.getInt("relayPin", DEFAULT_RELAY_PIN);
        settings.relayActiveHigh = prefs.getBool("relayActiveHigh", DEFAULT_RELAY_ACTIVE_HIGH);
        settings.debugMode = prefs.getBool("debugMode", DEFAULT_DEBUG_MODE);
        settings.mode2Enabled = prefs.getBool("mode2", false);

        settings.displayType = prefs.getInt("dispType_v2", DEFAULT_DISPLAY_TYPE);
        settings.tftCS = prefs.getInt("tftCS", DEFAULT_TFT_CS);
        settings.tftDC = prefs.getInt("tftDC", DEFAULT_TFT_DC);
        settings.tftRST = prefs.getInt("tftRST", DEFAULT_TFT_RST);
        settings.tftBL = prefs.getInt("tftBL", DEFAULT_TFT_BL);
        settings.tftRotation = prefs.getInt("tftRot", 1);

        settings.ruleCount = prefs.getInt("ruleCount", 0);
        if (settings.ruleCount <= 0 || settings.ruleCount > MAX_RULES || !prefs.isKey("rules_v3")) {
            settings.ruleCount = 3;
            {
                PaymentRule r = { 1.0f, 10, RELAY_MODE_TIMER, 1, DEFAULT_PULSE_ON_SEC, DEFAULT_PULSE_OFF_SEC, DEFAULT_PULSE_WAIT_SEC, "Best of luck for the game" };
                settings.rules[0] = r;
            }
            {
                PaymentRule r = { 5.0f, 60, RELAY_MODE_TIMER, 1, DEFAULT_PULSE_ON_SEC, DEFAULT_PULSE_OFF_SEC, DEFAULT_PULSE_WAIT_SEC, "Best of luck for the game" };
                settings.rules[1] = r;
            }
            {
                PaymentRule r = { 10.0f, 180, RELAY_MODE_TIMER, 1, DEFAULT_PULSE_ON_SEC, DEFAULT_PULSE_OFF_SEC, DEFAULT_PULSE_WAIT_SEC, "Best of luck for the game" };
                settings.rules[2] = r;
            }
            saveSettings();
            prefs.putBool("rules_v3", true);
        } else {
            for (int i = 0; i < settings.ruleCount; i++) {
                char keyA[16], keyD[16];
                char keyM[16], keyPC[16], keyPO[16], keyPF[16], keyPW[16], keyMSG[16];
                snprintf(keyA, sizeof(keyA), "r_%d_amt", i);
                snprintf(keyD, sizeof(keyD), "r_%d_dur", i);
                snprintf(keyM, sizeof(keyM), "r_%d_mode", i);
                snprintf(keyPC, sizeof(keyPC), "r_%d_pcnt", i);
                snprintf(keyPO, sizeof(keyPO), "r_%d_pon", i);
                snprintf(keyPF, sizeof(keyPF), "r_%d_poff", i);
                snprintf(keyPW, sizeof(keyPW), "r_%d_pwait", i);
                snprintf(keyMSG, sizeof(keyMSG), "r_%d_msg", i);
                settings.rules[i].amount = prefs.getFloat(keyA, 1.0f);
                settings.rules[i].durationSec = prefs.getUInt(keyD, 3);
                settings.rules[i].mode = (uint8_t)prefs.getUInt(keyM, (uint32_t)RELAY_MODE_TIMER);
                settings.rules[i].pulseCount = prefs.getUInt(keyPC, DEFAULT_PULSE_COUNT);
                settings.rules[i].pulseOnSec = prefs.getUInt(keyPO, DEFAULT_PULSE_ON_SEC);
                settings.rules[i].pulseOffSec = prefs.getUInt(keyPF, DEFAULT_PULSE_OFF_SEC);
                settings.rules[i].pulseWaitSec = prefs.getUInt(keyPW, DEFAULT_PULSE_WAIT_SEC);
                prefs.getString(keyMSG, settings.rules[i].pulseMessage, sizeof(settings.rules[i].pulseMessage));
                if (settings.rules[i].pulseMessage[0] == '\0') {
                    strncpy(settings.rules[i].pulseMessage, "Best of luck for the game", sizeof(settings.rules[i].pulseMessage) - 1);
                }
            }
        }

        Serial.println("[STORAGE] Settings successfully loaded from NVS.");
    }

    void saveSettings() {
        prefs.putString("deviceName", settings.deviceName);
        prefs.putBool("svcEnabled", settings.serviceEnabled);
        prefs.putString("apSSID", settings.apSSID);
        prefs.putString("apPass", settings.apPass);
        prefs.putString("staSSID", settings.staSSID);
        prefs.putString("staPass", settings.staPass);
        prefs.putString("adminUser", settings.adminUser);
        prefs.putString("adminPass", settings.adminPass);
        prefs.putString("backendUrl", settings.backendUrl);
        prefs.putString("deviceKey", settings.deviceKey);

        prefs.putInt("relayPin", settings.relayPin);
        prefs.putBool("relayActiveHigh", settings.relayActiveHigh);
        prefs.putBool("debugMode", settings.debugMode);
        prefs.putBool("mode2", settings.mode2Enabled);

        prefs.putInt("dispType", settings.displayType);
        prefs.putInt("dispType_v2", settings.displayType);
        prefs.putInt("tftCS", settings.tftCS);
        prefs.putInt("tftDC", settings.tftDC);
        prefs.putInt("tftRST", settings.tftRST);
        prefs.putInt("tftBL", settings.tftBL);
        prefs.putInt("tftRot", settings.tftRotation);

        prefs.putInt("ruleCount", settings.ruleCount);
        for (int i = 0; i < settings.ruleCount; i++) {
            char keyA[16], keyD[16];
            char keyM[16], keyPC[16], keyPO[16], keyPF[16], keyPW[16], keyMSG[16];
            snprintf(keyA, sizeof(keyA), "r_%d_amt", i);
            snprintf(keyD, sizeof(keyD), "r_%d_dur", i);
            snprintf(keyM, sizeof(keyM), "r_%d_mode", i);
            snprintf(keyPC, sizeof(keyPC), "r_%d_pcnt", i);
            snprintf(keyPO, sizeof(keyPO), "r_%d_pon", i);
            snprintf(keyPF, sizeof(keyPF), "r_%d_poff", i);
            snprintf(keyPW, sizeof(keyPW), "r_%d_pwait", i);
            snprintf(keyMSG, sizeof(keyMSG), "r_%d_msg", i);
            prefs.putFloat(keyA, settings.rules[i].amount);
            prefs.putUInt(keyD, settings.rules[i].durationSec);
            prefs.putUInt(keyM, (uint32_t)settings.rules[i].mode);
            prefs.putUInt(keyPC, settings.rules[i].pulseCount);
            prefs.putUInt(keyPO, settings.rules[i].pulseOnSec);
            prefs.putUInt(keyPF, settings.rules[i].pulseOffSec);
            prefs.putUInt(keyPW, settings.rules[i].pulseWaitSec);
            prefs.putString(keyMSG, settings.rules[i].pulseMessage);
        }

        Serial.println("[STORAGE] Settings persisted to NVS.");
    }

    DeviceSettings& getSettings() {
        return settings;
    }

    // ==========================================
    // Transaction History & Anti-Replay
    // ==========================================
    void loadTransactions() {
        transactionCount = prefs.getInt("txCount", 0);
        if (transactionCount > MAX_TRANSACTIONS_LOGGED) transactionCount = MAX_TRANSACTIONS_LOGGED;
        for (int i = 0; i < transactionCount; i++) {
            char key[20];
            snprintf(key, sizeof(key), "tx_%d", i);
            prefs.getBytes(key, &transactions[i], sizeof(TransactionRecord));
        }
    }

    bool isPaymentConsumed(const char* paymentId) {
        if (!paymentId || strlen(paymentId) == 0) return false;
        for (int i = 0; i < transactionCount; i++) {
            if (strncmp(transactions[i].paymentId, paymentId, sizeof(transactions[i].paymentId)) == 0) {
                return true; // Already processed!
            }
        }
        return false;
    }

    void logTransaction(const char* qrId, const char* paymentId, float amount, uint32_t durationSec, bool success) {
        // Shift records if at capacity
        if (transactionCount >= MAX_TRANSACTIONS_LOGGED) {
            for (int i = 0; i < MAX_TRANSACTIONS_LOGGED - 1; i++) {
                transactions[i] = transactions[i + 1];
            }
            transactionCount = MAX_TRANSACTIONS_LOGGED - 1;
        }

        TransactionRecord& record = transactions[transactionCount];
        strncpy(record.qrId, qrId ? qrId : "", sizeof(record.qrId) - 1);
        strncpy(record.paymentId, paymentId ? paymentId : "", sizeof(record.paymentId) - 1);
        record.amount = amount;
        record.durationSec = durationSec;
        record.timestampSec = millis() / 1000;
        record.success = success;
        transactionCount++;

        prefs.putInt("txCount", transactionCount);
        char key[20];
        snprintf(key, sizeof(key), "tx_%d", transactionCount - 1);
        prefs.putBytes(key, &record, sizeof(TransactionRecord));

        Serial.printf("[STORAGE] Logged transaction: PayID=%s, Amt=%.2f, Dur=%us, Success=%d\n",
                      record.paymentId, record.amount, record.durationSec, record.success);
    }

    int getTransactionCount() const { return transactionCount; }
    const TransactionRecord* getTransactions() const { return transactions; }

    uint32_t getDurationForAmount(float amount) {
        for (int i = 0; i < settings.ruleCount; i++) {
            if (abs(settings.rules[i].amount - amount) < 0.01f) {
                return settings.rules[i].durationSec;
            }
        }
        // Fallback default: 3 seconds
        return 3;
    }
};

#endif // STORAGE_MANAGER_H
