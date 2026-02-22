# S3-Pony-Keeb

A BLE HID keyboard built on the **DFRobot FireBeetle 2 ESP32-S3**. Pairs with a phone over Bluetooth, acts as a native keyboard, and sends Space when a button is pressed. Sleeps to save power and wakes on button press.

## What It Does

1. **Bluetooth pairing** — pairs with a phone as a standard BLE keyboard (no app needed)
2. **HID keyboard** — sends the Space key when the Action button is pressed
3. **Sleep & wake** — enters sleep mode when idle; wakes on button press and sends the keystroke

## Development Extras

Added during development for debugging and usability — not part of the original spec:

- **3 LEDs** (red, orange, yellow) as a status dashboard
- **Config button** for clearing Bluetooth bonds and re-pairing
- **Three-tier power management** (full speed → auto light sleep → deep sleep) instead of simple deep sleep
- **Serial debug logging** via USB CDC
- **NimBLE stack** — lighter on RAM and power than the default BLE stack

## Hardware

### Components

| Component     | Board Pin | GPIO    | Notes                               |
| ------------- | --------- | ------- | ----------------------------------- |
| Red LED       | D5        | GPIO 7  | Boot / wake indicator (220 Ω)       |
| Orange LED    | D3        | GPIO 38 | BLE connection status (220 Ω)       |
| Yellow LED    | D2        | GPIO 3  | Keystroke confirmation (220 Ω)      |
| Action Button | D6        | GPIO 18 | Sends Space / wakes from deep sleep |
| Config Button | D7        | GPIO 9  | Bond clear (hold 3 s) / pairing     |

### Wiring

```
GPIO 18 ── [BTN] ── [10kΩ] ── 3V3
GPIO 9  ── [BTN] ── [10kΩ] ── 3V3

GPIO 7  ── [220Ω] ── LED (Red)    ── GND
GPIO 38 ── [220Ω] ── LED (Orange) ── GND
GPIO 3  ── [220Ω] ── LED (Yellow) ── GND
```

Each button has one pin connected to the GPIO and the diagonal pin wired to **3V3 through a 10 kΩ resistor**. Pressing the button connects the GPIO to 3V3, reading **HIGH**. The code triggers on HIGH.

## Power Management

| State                | Trigger                        | Estimated Current | BLE       | Wake          |
| -------------------- | ------------------------------ | ----------------- | --------- | ------------- |
| Full speed (240 MHz) | Active use                     | ~100 mA           | Connected | —             |
| Auto light sleep     | 10 s idle                      | ~5–15 mA          | Connected | Instant       |
| Deep sleep           | 30 s idle / 2 min disconnected | ~10–20 µA         | Off       | Action button |

Auto light sleep uses `esp_pm_configure()` so the CPU naps between BLE connection intervals. BLE stays connected — no reconnect delay on wake.

## LED States

| LED             | Meaning                    |
| --------------- | -------------------------- |
| Red ON          | Board is powered           |
| Orange blinking | Waiting for BLE connection |
| Orange solid    | BLE connected              |
| Yellow flash    | Keystroke sent             |

## Build & Upload

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)

### Build

```bash
pio run
```

### Upload

```bash
pio run --target upload
```

> **If the board isn't detected:** the ESP32-S3 USB CDC only exists while firmware is running. If the board is stuck in deep sleep, enter bootloader mode: **hold BOOT → plug in USB → release BOOT**. Then upload or erase flash with:
>
> ```bash
> esptool.py --port /dev/ttyACM0 erase_flash
> ```

```
Button wiring (active-high):

GPIO ── [BTN] ── 3V3 (diagonally opposite pin)
          |
          └─── [10kΩ] ── GND (same side as GPIO)

For each button:
- Connect one pin to GPIO input
- Connect the diagonally opposite pin to 3V3
- Place a 10kΩ pull-down resistor between the GPIO pin and ground (on the same side as GPIO)
- The other two pins (on the same side as the first two) can be left unconnected or used for mechanical stability

LED wiring:
GPIO 7  ── [220Ω] ── LED (Red)    ── GND
GPIO 38 ── [220Ω] ── LED (Orange) ── GND
```

The ESP32-S3 appears as `/dev/ttyACM0` or `/dev/ttyACM1`. The port number may change after a deep sleep reboot. Set `monitor_port` in `platformio.ini` if needed.

## Usage

1. Power on the board — **Red LED** turns on.
2. **Orange LED** blinks while advertising over BLE.
3. Pair from your phone/PC — look for **"S3-Pony-Keeb"** in Bluetooth settings.
4. **Orange LED** goes solid when connected.
5. Press the **Action button** — sends Space, **Yellow LED** flashes.
6. To re-pair: hold **Config button** for 3 seconds to clear bonds and restart.

## Project Structure

```
├── src/
│   └── main.cpp            # Firmware source
├── docs/
│   ├── specifications.md   # Hardware & functional spec
│   └── development-log.md  # Build diary & lessons learned
├── platformio.ini           # Build config, libraries, ports
├── include/                 # Header files (unused)
├── lib/                     # Project-specific libraries (unused)
└── test/                    # Unit tests (unused)
```

## License

Unlicensed — personal project.
