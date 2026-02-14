# Sleep Modes — How & Why

This document covers the ESP32-S3 sleep modes, which ones we tried, what worked, what didn't, and why we ended up with a three-tier system.

## ESP32-S3 Sleep Modes Overview

The ESP32-S3 has four power states, from most to least power-hungry:

| Mode            | CPU    | RAM      | Peripherals | WiFi / BLE | Typical Current |
| --------------- | ------ | -------- | ----------- | ---------- | --------------- |
| **Active**      | On     | On       | On          | On         | 80–240 mA       |
| **Modem sleep** | On     | On       | On          | Off        | 20–30 mA        |
| **Light sleep** | Paused | Retained | Mostly off  | Off\*      | 0.3–2 mA        |
| **Deep sleep**  | Off    | Lost     | Off         | Off        | 10–20 µA        |

\* With automatic light sleep via power management, BLE stays connected — see below.

> **References:**
>
> - [ESP-IDF Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/sleep_modes.html)
> - [ESP-IDF Power Management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/power_management.html)

---

## What We Tried

### Attempt 1: Deep Sleep Only

**Idea:** Keep the board in deep sleep. Wake on button press, send the keystroke, go back to sleep.

**How deep sleep works:**

- The main CPUs and most RAM are powered off
- Only the **ULP coprocessor** and **RTC memory** stay alive
- Wake-up sources: RTC timer, `ext0` / `ext1` (GPIO), ULP, touch
- On wake the board performs a **full reboot** — `setup()` runs from scratch

```cpp
esp_sleep_enable_ext0_wakeup(B_ACTION, 1);  // wake when GPIO goes HIGH
esp_deep_sleep_start();
```

> [ext0 wakeup docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/sleep_modes.html#ext0-wakeup)

**Why it failed:**

1. **BLE dies completely.** Every wake requires: reboot → BLE init → advertising → phone reconnect. This takes **3–8 seconds** — way too slow for a button press.
2. **USB CDC disappears.** The ESP32-S3's native USB is software-driven. Deep sleep kills it, so Serial Monitor and uploads stop working until you manually enter bootloader mode.
3. **False wake-ups.** Internal pull-ups turn off during deep sleep. Without external pull-up/pull-down resistors the floating pin triggers an immediate wake.

**Verdict:** Only usable as a last-resort timeout, not for active use.

---

### Attempt 2: Manual Light Sleep

**Idea:** Use light sleep instead. RAM is retained so BLE state should survive.

**How manual light sleep works:**

- CPU execution is paused (not rebooted)
- RAM contents are preserved
- You call `esp_light_sleep_start()` and execution resumes from the same line when a wake source triggers
- Wake sources: GPIO (`gpio_wakeup_enable`), timer, UART

```cpp
gpio_wakeup_enable(B_ACTION, GPIO_INTR_HIGH_LEVEL);
esp_sleep_enable_gpio_wakeup();
esp_light_sleep_start();
// execution resumes here on wake
```

> [GPIO wakeup for light sleep](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/sleep_modes.html#gpio-wakeup)

**Why it failed:**

- `esp_light_sleep_start()` **still tears down the BLE radio stack**. On wake, a full BLE reconnection is needed — almost the same delay as deep sleep, just slightly faster.
- The radio is explicitly powered down during manual light sleep entry.

**Verdict:** No better than deep sleep for BLE use cases.

---

### Attempt 3: Automatic Light Sleep via Power Management (adopted)

**Idea:** Instead of manually entering sleep, let the ESP-IDF **power management framework** automatically put the CPU to sleep during idle periods between BLE connection intervals.

**How it works:**

- `esp_pm_configure()` sets a min/max CPU frequency and enables automatic light sleep
- The system uses a **lock mechanism**: when a peripheral (like BLE) holds a power lock, the CPU stays awake; when all locks are released (between BLE events), the CPU automatically naps
- BLE holds its lock during connection events (~2–4 ms every 15–30 ms) and releases it in between
- The CPU frequency drops to `min_freq_mhz` during idle and ramps up to `max_freq_mhz` when active
- **BLE stays fully connected** because the radio wakes on its own schedule

```cpp
esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 10,
    .light_sleep_enable = true
};
esp_pm_configure(&pm_config);
```

> [Power management API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/power_management.html)
> [Power management locks](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/power_management.html#power-management-locks)

**Build flag required:**

```ini
build_flags = -D CONFIG_PM_ENABLE=1
```

**Why it works:**

- BLE connection intervals are handled by the radio's own wake schedule — the framework knows not to sleep through them
- Button presses are responsive because the CPU wakes on any interrupt
- No manual sleep/wake code needed — the system handles it transparently
- Measured current drops to **~5–15 mA** (vs ~100 mA at full speed)

**Verdict:** This is what we use for the "idle but connected" tier.

---

## The Three-Tier System

We combine automatic light sleep with deep sleep as a last resort:

```
Active use                          Full speed (240 MHz)
    │
    ├── 10s no key press ──────►    Auto light sleep (80/10 MHz, BLE connected)
    │
    ├── 30s no key press ──────►    Deep sleep (µA, BLE off, full reboot on wake)
    │
    └── 2 min disconnected ────►    Deep sleep
```

### Tier 1: Full Speed

- CPU at 240 MHz, everything on
- Triggered by any button press (disables power saving)
- Ensures responsive keystroke handling

```cpp
void disablePowerSaving() {
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 240,
        .light_sleep_enable = false
    };
    esp_pm_configure(&pm_config);
}
```

### Tier 2: Auto Light Sleep

- CPU capped at 80 MHz, drops to 10 MHz when idle, auto-sleeps between BLE intervals
- BLE **stays connected** — wake is instant, no reconnect
- Kicks in after 10 seconds of no key press

### Tier 3: Deep Sleep

- Everything off except RTC, ~10–20 µA
- Used only after **30 s idle** (connected) or **2 min disconnected**
- Wake source: Action button via `ext0_wakeup`
- Full reboot on wake — BLE must reconnect (3–8 s)

---

## Key Takeaways

| Lesson                           | Detail                                                                                               |
| -------------------------------- | ---------------------------------------------------------------------------------------------------- |
| Manual light sleep kills BLE     | `esp_light_sleep_start()` powers down the radio — don't use it if you need BLE                       |
| Auto light sleep keeps BLE alive | `esp_pm_configure()` with `light_sleep_enable = true` is the right tool                              |
| Deep sleep = full reboot         | Everything including RAM is lost; only use as a last resort                                          |
| USB CDC dies in deep sleep       | The ESP32-S3 native USB is software-driven and stops during any sleep — plan for bootloader recovery |
| `ext0_wakeup` needs stable pins  | Use external resistors on wake pins — internal pull-ups turn off in deep sleep                       |

## Further Reading

- [ESP-IDF Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/sleep_modes.html)
- [ESP-IDF Power Management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/power_management.html)
- [ESP32-S3 Technical Reference Manual — Chapter 10: Low-Power Management](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [NimBLE and power saving (h2zero)](https://github.com/h2zero/NimBLE-Arduino)
- [Arduino-ESP32 Power Management example](https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/DeepSleep)
