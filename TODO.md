# S3-Pony-Keeb — TODO

## 1. Better Power Management

The current three-tier system (full speed → auto light sleep → deep sleep) is functional but there's room to squeeze out more efficiency, especially given that this is a battery-oriented BLE device that sits idle most of the time.

### Disable the Second Core

The ESP32-S3 is a dual-core chip (two Xtensa LX7 cores). Right now both cores are active even though the firmware is single-threaded — BLE and `loop()` all run on core 0 (the Arduino default). Core 1 is powered on but doing nothing useful, wasting power.

**Options to investigate:**

- **Disable at build time** — The ESP-IDF menuconfig has a `CONFIG_FREERTOS_UNICORE` option that boots only one core. This is the cleanest approach: the second core never starts, and the RTOS scheduler only targets core 0. Need to check if this can be set via `build_flags` in `platformio.ini` (`-D CONFIG_FREERTOS_UNICORE=1`) or if it requires `sdkconfig` modification.
- **Set core 1 clock to 0 MHz in software** — Look into whether `esp_pm_configure()` or the clock tree API allows per-core frequency control. Unlikely on ESP32-S3 since both cores share the same clock source, but worth confirming.
- **Pin all tasks to core 0** — If disabling the core entirely isn't feasible, ensure all FreeRTOS tasks (Arduino loop, BLE stack, timers) are pinned to core 0 so core 1 can at least idle fully and be clock-gated by the power management framework.

### Other Power Ideas (Stretch)

- **Lower `max_freq_mhz` further** — Currently 80 MHz during power saving. BLE HID might work fine at 40 MHz since the radio is largely hardware-driven.
- **Increase BLE connection interval** — Request a longer connection interval from the host (e.g., 100–200 ms instead of the default 15–30 ms). Fewer radio wake-ups = less power. Acceptable for a single-key keyboard since latency tolerance is high.
- **Disable unused peripherals** — Wi-Fi, ADC, I2C, SPI are all initialized by default. Explicitly powering them down could save a few mA.
- **LED power** — Ties into the RGB LED migration below (see §4). A single addressable LED is inherently more power-efficient than three discrete LEDs with always-on GPIOs.

---

## 2. Single Button for Both Connectivity and Action

Currently there are two buttons:

- **Action (GPIO 18)** — sends Space over BLE, wakes from deep sleep
- **Config (GPIO 9)** — hold at boot to clear bonds, hold 3s while running to clear bonds and restart

The goal is to merge both functions into a single physical button, reducing component count and simplifying the hardware. This means the one button needs to handle:

### Proposed Behavior

| Gesture             | Action                            |
| ------------------- | --------------------------------- |
| **Short press**     | Send Space keystroke (HID)        |
| **Long press (3s)** | Clear Bluetooth bonds and restart |
| **Hold at boot**    | Enter pairing/bond-clear mode     |
| **Press in sleep**  | Wake from deep sleep              |

### Implementation Notes

- **Debounce + hold detection** — On press, start a timer. If released before the long-press threshold (e.g., 3 seconds), treat it as a short press and send Space. If held past the threshold, treat it as a config action. Need to be careful not to also send a Space on long press.
- **Wake from deep sleep** — Already works on the action button via `ext0_wakeup`. No change needed here, just wire the single button to the same GPIO.
- **Boot-time bond clear** — Currently checks Config button in `setup()`. Change to check the Action button GPIO instead.
- **Hardware** — Remove the Config button and its pull-up resistor from the breadboard. One fewer GPIO used (GPIO 9 freed up).
- **LED feedback** — Use the yellow LED to distinguish: quick flash = keystroke, sustained blink pattern = bond clear happening.

---

## 3. Better Debugging Prints

The current serial output works but is inconsistent in format and light on detail. When things go wrong (BLE won't connect, sleep doesn't trigger, button bounces), the logs don't always give enough information to diagnose.

### Current State

- Prefixed tags like `[PWR]`, `[BLE]`, `[HID]`, `[DBG]` — good, but not used consistently
- No timestamps on log lines — hard to correlate events or measure timing
- No indication of which core is executing (relevant if we keep dual-core)
- Power state transitions are logged but current frequency / sleep stats are not
- Button state is only printed at boot, not on each press

### Improvements

- **Add timestamps** — Prefix every log line with `millis()` so timing between events is visible at a glance. Format: `[12345][PWR] Entering deep sleep...`
- **Consistent tag usage** — Standardize on the existing tags and use them everywhere:
  - `[PWR]` — power state changes, sleep entry/exit
  - `[BLE]` — connection, disconnection, advertising
  - `[HID]` — keystroke send/release
  - `[BTN]` — button press, debounce, hold detection
  - `[SYS]` — boot, restart, bond clear
- **Log levels** — Use a simple `#define DEBUG_LEVEL` to control verbosity. Level 0 = off (for production), level 1 = important events only, level 2 = everything including raw button states and timing.
- **Debug macro** — Replace raw `Serial.println()` / `Serial.printf()` calls with a macro that handles timestamps, tags, and level filtering in one place:

  ```cpp
  #define DBG(level, tag, fmt, ...) \
      do { if (DEBUG_LEVEL >= level) \
          Serial.printf("[%lu][" tag "] " fmt "\n", millis(), ##__VA_ARGS__); \
      } while(0)

  // Usage:
  DBG(1, "BLE", "Connected to host");
  DBG(2, "BTN", "Action button state: %d, held for %lu ms", state, holdTime);
  ```

- **Boot summary** — Print a compact system info block at boot: firmware version, CPU frequency, free heap, BLE MAC address, wake reason (if waking from deep sleep vs. cold boot).
- **BLE event detail** — Log the connection interval, MTU, and peer address on connect. Log the disconnect reason code on disconnect.

---

## 4. Replace 3 LEDs with a Single RGB LED (SK6812)

The current design uses three discrete LEDs (red, orange, yellow) each on their own GPIO with a 220 Ω resistor. The final project will replace all three with a single addressable RGB LED — likely an **SK6812** (or WS2812B-compatible).

### Why SK6812

- **One data pin** — All color control over a single GPIO, freeing up two pins (currently using GPIO 7, 38, 3 for LEDs).
- **Built-in current regulation** — No external resistors needed.
- **Full color range** — Can represent all the current indicators (red = boot, orange = BLE, yellow = keystroke) and more, using any RGB color or even patterns/animations.
- **RGBW variant available** — The SK6812 comes in an RGBW version with a dedicated white channel, useful for clean white status indication.

### Color Mapping (Proposed)

| State                  | Color          | Pattern      |
| ---------------------- | -------------- | ------------ |
| Boot / wake            | Red            | Solid, brief |
| BLE advertising        | Orange         | Slow blink   |
| BLE connected          | Green          | Solid        |
| Keystroke sent         | Yellow / White | Quick flash  |
| Bond clear in progress | Purple         | Fast blink   |
| Power saving active    | Dim blue       | Breathing    |
| Entering deep sleep    | Off            | Fade out     |

### Implementation Notes

- **Library** — Use a lightweight NeoPixel-compatible library. Options: Adafruit NeoPixel, FastLED, or the ESP-IDF RMT driver directly. FastLED is heavier; the RMT driver is the most efficient for a single LED.
- **Power** — A single SK6812 draws ~20 mA at max white. At typical indicator brightness (10–20%), it'll draw 2–5 mA — significantly less than three always-on LEDs. Can be turned fully off during power saving.
- **GPIO choice** — Pick one of the freed-up GPIOs (e.g., GPIO 7) for the data line.
- **Timing** — SK6812 uses 800 kHz one-wire protocol. The ESP32-S3 RMT peripheral can generate this in hardware without CPU involvement, which plays well with the power management / light sleep system.

---

## 5. Lower Main Core Frequency

The current firmware runs at 240 MHz during active use, which is massive overkill for a single-button BLE HID device. The goal is to find the lowest stable frequency that keeps the system responsive.

### Current Situation

- **Active:** 240 MHz — set by `disablePowerSaving()` when a key is pressed
- **Power saving:** 80 MHz max / 10 MHz min — set by `enablePowerSaving()` after 10s idle

Both are higher than necessary. The workload is: poll one GPIO, run the BLE stack, drive one LED. None of this needs 240 MHz.

### Target

- **Active `max_freq_mhz`:** Try **80 MHz** instead of 240 MHz. The BLE radio has its own clock and doesn't depend on CPU frequency. Button debounce and LED toggling are trivially fast. If 80 MHz feels instant, try **40 MHz**.
- **Power saving `max_freq_mhz`:** Try **40 MHz** instead of 80 MHz. During power saving, the only work happening is BLE keepalive (hardware-driven) and occasional timer checks.
- **`min_freq_mhz`:** Keep at **10 MHz** (or try lower if the auto light sleep framework allows it). This is what the CPU drops to between BLE events.

### Testing Approach

1. Set `max_freq_mhz = 80` for the active state and verify BLE connection stability, keystroke latency, and LED responsiveness.
2. If stable, drop to `max_freq_mhz = 40` and repeat.
3. For power saving, test `max_freq_mhz = 40` with `min_freq_mhz = 10`.
4. Measure actual current draw at each level if possible (USB current meter or INA219 breakout).
5. Check that the SK6812 data signal (800 kHz) is still generated correctly at lower CPU frequencies — this depends on RMT peripheral clock, which is typically independent of the CPU clock, but should be verified.

### Constraints

- The **BLE stack** may have a minimum CPU frequency requirement. NimBLE on ESP-IDF generally works at 80 MHz; 40 MHz needs testing.
- **USB CDC** (Serial Monitor) may not work reliably below 80 MHz on ESP32-S3. If debugging prints are needed, keep 80 MHz as the floor during development and only drop lower for the final build.
- The **RMT peripheral** (for SK6812) has its own clock divider and should work independently, but the driver initialization might assume a certain APB clock frequency.
