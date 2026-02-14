#include <Arduino.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>
#include <esp_sleep.h>
#include <esp_pm.h>

// Pin Mapping for FireBeetle 2 ESP32-S3
// LEDs (each with 220Ω resistor)
const gpio_num_t L_RED    = GPIO_NUM_7;   // D5 - Red LED (Interrupt/Wake indicator)
const gpio_num_t L_ORANGE = GPIO_NUM_38;  // D3 - Orange LED (BLE status)
const gpio_num_t L_YELLOW = GPIO_NUM_3;   // D2 - Yellow LED (Keystroke confirmation)

// Buttons (10kΩ pull-up to 3V3, button connects to GND when pressed → LOW)
const gpio_num_t B_ACTION = GPIO_NUM_18;  // D6 - Action Button (sends Space)
const gpio_num_t B_CONFIG = GPIO_NUM_9;   // D7 - Config Button (Pairing / Reset)

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
    Serial.println("[PWR] Entering DEEP SLEEP. Press Action to wake.");
    Serial.flush();
    esp_sleep_enable_ext0_wakeup(B_ACTION, 1);  // Wake on button HIGH
    esp_deep_sleep_start();
}

void enablePowerSaving() {
    // Auto light sleep: CPU sleeps between BLE intervals, BLE stays connected
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 80,     // Cap CPU at 80MHz
        .min_freq_mhz = 10,     // Drop to 10MHz when idle
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
    powerSaving = true;
    Serial.println("[PWR] Power saving ON (auto light sleep, BLE stays connected)");
}

void disablePowerSaving() {
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 240,
        .light_sleep_enable = false
    };
    esp_pm_configure(&pm_config);
    powerSaving = false;
    Serial.println("[PWR] Power saving OFF (full speed)");
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
    pinMode(B_CONFIG, INPUT);

    // Red LED ON = processor booted
    digitalWrite(L_RED, HIGH);
    Serial.println("===============================");
    Serial.println("   BOOT: S3-Pony-Keeb          ");
    Serial.println("===============================");

    // Print button states
    Serial.printf("[DBG] Action (GPIO%d): %d\n", B_ACTION, digitalRead(B_ACTION));
    Serial.printf("[DBG] Config (GPIO%d): %d\n", B_CONFIG, digitalRead(B_CONFIG));

    // BOND RESET: Hold Config (D7) at boot to clear all Bluetooth bonds
    if (digitalRead(B_CONFIG) == HIGH) {
        Serial.println("[!] RESET: Clearing Bluetooth bonds...");
        NimBLEDevice::deleteAllBonds();
        for (int i = 0; i < 10; i++) {
            digitalWrite(L_YELLOW, HIGH); delay(50);
            digitalWrite(L_YELLOW, LOW);  delay(50);
        }
    }

    bleKeyboard.begin();
    lastKeyPress = millis();
    lastConnectedTime = millis();
    Serial.println("[BLE] Advertising started. Waiting for connection...");
}

void loop() {
    bool isConnected = bleKeyboard.isConnected();
    unsigned long now = millis();

    // --- Connection state change detection ---
    if (isConnected && !wasConnected) {
        digitalWrite(L_ORANGE, HIGH);
        Serial.println("[BLE] CONNECTED!");
        wasConnected = true;
        lastKeyPress = now;
        lastConnectedTime = now;
    } else if (!isConnected && wasConnected) {
        digitalWrite(L_ORANGE, LOW);
        Serial.println("[BLE] Disconnected. Waiting for reconnection...");
        wasConnected = false;
        lastConnectedTime = now;
    }

    // --- Disconnected: blink orange, deep sleep after 2 min ---
    if (!isConnected) {
        digitalWrite(L_ORANGE, (now / 500) % 2);

        if (now - lastConnectedTime > DISCONNECTED_TIMEOUT) {
            Serial.println("[PWR] No connection for 2 min.");
            enterDeepSleep();
        }

        delay(50);
        return;
    }

    // --- Connected idle timeouts ---
    unsigned long idleTime = now - lastKeyPress;

    // 30s no key press → deep sleep
    if (idleTime > DEEP_SLEEP_KEY_TIMEOUT) {
        Serial.printf("[PWR] No key press for %lums.\n", idleTime);
        if (powerSaving) disablePowerSaving();
        enterDeepSleep();
    }

    // 10s no key press → enable power saving (auto light sleep, BLE stays alive)
    if (idleTime > POWER_SAVE_TIMEOUT && !powerSaving) {
        enablePowerSaving();
    }

    // --- Action Button: send Space (HIGH = pressed) ---
    if (digitalRead(B_ACTION) == HIGH && (now - lastActionPress > DEBOUNCE_MS)) {
        lastActionPress = now;
        lastKeyPress = now;
        // Go back to full speed for responsive keystroke handling
        if (powerSaving) disablePowerSaving();
        Serial.println("[HID] >>> Sending keystroke: SPACE <<<");
        digitalWrite(L_YELLOW, HIGH);
        bleKeyboard.press(' ');
        delay(50);
        bleKeyboard.releaseAll();
        delay(50);
        digitalWrite(L_YELLOW, LOW);
        Serial.println("[HID] Keystroke sent.");
    }

    // --- Config Button: held 3s = clear bonds & restart ---
    if (digitalRead(B_CONFIG) == HIGH) {
        Serial.println("[DBG] Config button pressed, timing hold...");
        unsigned long holdStart = millis();
        while (digitalRead(B_CONFIG) == HIGH && (millis() - holdStart < 3000)) {
            delay(10);
        }
        if (millis() - holdStart >= 3000) {
            Serial.println("[!] Config held 3s: Clearing bonds & restarting...");
            NimBLEDevice::deleteAllBonds();
            for (int i = 0; i < 6; i++) {
                digitalWrite(L_YELLOW, HIGH); delay(80);
                digitalWrite(L_YELLOW, LOW);  delay(80);
            }
            ESP.restart();
        } else {
            Serial.printf("[DBG] Config released after %lums (need 3000ms)\n", millis() - holdStart);
        }
    }

    delay(10);
}
