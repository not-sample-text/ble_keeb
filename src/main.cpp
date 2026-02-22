#include <Arduino.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>
#include <esp_sleep.h>
#include <esp_pm.h>
// Debug log levels
#define DEBUG_LEVEL 2

// Debug macro: timestamp, tag, level filtering
#define DBG(level, tag, fmt, ...) \
    do { if (DEBUG_LEVEL >= level) \
        Serial.printf("[%lu][%s] " fmt "\n", millis(), tag, ##__VA_ARGS__); \
    } while(0)

// Pin Mapping for FireBeetle 2 ESP32-S3
// LEDs (each with 220Ω resistor)
const gpio_num_t L_RED    = GPIO_NUM_7;   // D5 - Red LED (Interrupt/Wake indicator)
const gpio_num_t L_ORANGE = GPIO_NUM_38;  // D3 - Orange LED (BLE status)
const gpio_num_t L_YELLOW = GPIO_NUM_3;   // D2 - Yellow LED (Keystroke confirmation)

// Buttons (10kΩ pull-up to 3V3, button connects to GND when pressed → LOW)
const gpio_num_t B_ACTION = GPIO_NUM_18;  // D6 - Single Button (Space + bond clear)

BleKeyboard bleKeyboard("S3-Pony-Keeb", "TestTest", 100);

// Debounce tracking
unsigned long lastActionPress = 0;
const unsigned long DEBOUNCE_MS = 250;

// Connection state tracking
bool wasConnected = false;
bool powerSaving = false;

// Timeout thresholds
unsigned long lastKeyPress = 0;
unsigned long lastConnectedTime = 0;
const unsigned long POWER_SAVE_TIMEOUT    = 10000;   // 10s no key press → reduce CPU freq
const unsigned long DEEP_SLEEP_KEY_TIMEOUT = 30000;  // 30s no key press → deep sleep
const unsigned long DISCONNECTED_TIMEOUT   = 120000; // 2 min not connected → deep sleep

void enterDeepSleep() {
    DBG(1, "PWR", "Entering DEEP SLEEP. Press Action to wake.");
    Serial.flush();
    esp_sleep_enable_ext0_wakeup(B_ACTION, 1);  // Wake on button HIGH
    esp_deep_sleep_start();
}

void enablePowerSaving() {
    // Auto light sleep: CPU sleeps between BLE intervals, BLE stays connected
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 40,     // Cap CPU at 20MHz for extra power saving
        .min_freq_mhz = 10,     // Drop to 10MHz when idle
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
    powerSaving = true;
    DBG(1, "PWR", "Power saving ON (auto light sleep, BLE stays connected)");
}

void disablePowerSaving() {
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = false
    };
    esp_pm_configure(&pm_config);
    powerSaving = false;
    DBG(1, "PWR", "Power saving OFF (full speed)");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Configure LED pins
    pinMode(L_RED, OUTPUT);
    pinMode(L_ORANGE, OUTPUT);
    pinMode(L_YELLOW, OUTPUT);

    // Configure Button pins (external pull-down, read HIGH when pressed)
    pinMode(B_ACTION, INPUT);

    // Red LED ON = processor booted
    digitalWrite(L_RED, HIGH);
    DBG(1, "SYS", "===============================");
    DBG(1, "SYS", "   BOOT: S3-Pony-Keeb");
    DBG(1, "SYS", "===============================");

    // Print button state
    DBG(2, "BTN", "Action (GPIO%d): %d", B_ACTION, digitalRead(B_ACTION));

    // BOND RESET: Hold Action at boot to clear all Bluetooth bonds
    if (digitalRead(B_ACTION) == HIGH) {
        DBG(1, "SYS", "RESET: Clearing Bluetooth bonds...");
        NimBLEDevice::deleteAllBonds();
        for (int i = 0; i < 10; i++) {
            digitalWrite(L_YELLOW, HIGH); delay(50);
            digitalWrite(L_YELLOW, LOW);  delay(50);
        }
    }
    // If waking from deep sleep, send Space immediately
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        DBG(1, "HID", "Woke from deep sleep by Action button. Sending SPACE.");
        digitalWrite(L_YELLOW, HIGH);
        bleKeyboard.press(' ');
        delay(50);
        bleKeyboard.releaseAll();
        delay(50);
        digitalWrite(L_YELLOW, LOW);
        DBG(1, "HID", "Keystroke sent after wake.");
    }

    bleKeyboard.begin();
    lastKeyPress = millis();
    lastConnectedTime = millis();
    DBG(1, "BLE", "Advertising started. Waiting for connection...");
}

void loop() {
    bool isConnected = bleKeyboard.isConnected();
    unsigned long now = millis();

    // --- Connection state change detection ---
    if (isConnected && !wasConnected) {
        digitalWrite(L_ORANGE, HIGH);
        DBG(1, "BLE", "CONNECTED!");
        wasConnected = true;
        lastKeyPress = now;
        lastConnectedTime = now;
    } else if (!isConnected && wasConnected) {
        digitalWrite(L_ORANGE, LOW);
        DBG(1, "BLE", "Disconnected. Waiting for reconnection...");
        wasConnected = false;
        lastConnectedTime = now;
    }

    // --- Disconnected: blink orange, deep sleep after 2 min ---
    if (!isConnected) {
        digitalWrite(L_ORANGE, (now / 500) % 2);

        if (now - lastConnectedTime > DISCONNECTED_TIMEOUT) {
            DBG(1, "PWR", "No connection for 2 min.");
            Serial.flush();
            enterDeepSleep();
        }

        delay(50);
        return;
    }

    // --- Connected idle timeouts ---
    unsigned long idleTime = now - lastKeyPress;

    // 30s no key press → deep sleep
    if (idleTime > DEEP_SLEEP_KEY_TIMEOUT) {
        DBG(1, "PWR", "No key press for %lums.", idleTime);
        Serial.flush();
        if (powerSaving) disablePowerSaving();
        enterDeepSleep();
    }

    // 10s no key press → enable power saving (auto light sleep, BLE stays alive)
    if (idleTime > POWER_SAVE_TIMEOUT && !powerSaving) {
        DBG(2, "PWR", "Idle for %lums, enabling power saving.", idleTime);
        enablePowerSaving();
    }

    // --- Single Button: short press = Space, long press (3s) = bond clear & restart ---
    if (digitalRead(B_ACTION) == HIGH && (now - lastActionPress > DEBOUNCE_MS)) {
        unsigned long pressStart = millis();
        DBG(2, "BTN", "Action button pressed, starting hold detection.");
        // Wait for release or long press
        while (digitalRead(B_ACTION) == HIGH) {
            unsigned long holdTime = millis() - pressStart;
            if (holdTime >= 3000) {
                DBG(1, "SYS", "Action held 3s: Clearing bonds & restarting...");
                NimBLEDevice::deleteAllBonds();
                for (int i = 0; i < 6; i++) {
                    digitalWrite(L_YELLOW, HIGH); delay(80);
                    digitalWrite(L_YELLOW, LOW);  delay(80);
                }
                ESP.restart();
                return;
            }
            delay(10);
        }
        // Short press: send Space
        lastActionPress = now;
        lastKeyPress = now;
        if (powerSaving) disablePowerSaving();
        DBG(1, "HID", ">>> Sending keystroke: SPACE <<<");
        digitalWrite(L_YELLOW, HIGH);
        bleKeyboard.press(' ');
        delay(50);
        bleKeyboard.releaseAll();
        delay(50);
        digitalWrite(L_YELLOW, LOW);
        DBG(1, "HID", "Keystroke sent.");
    }

    delay(10);
}
