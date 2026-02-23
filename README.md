# S3-Pony-Keeb

A BLE HID keyboard built on the **DFRobot FireBeetle 2 ESP32-S3**. Pairs with a phone over Bluetooth, acts as a native keyboard, and sends Space when a button is pressed. Sleeps to save power and wakes on button press.

## Features

1. **Bluetooth pairing** — pairs with a phone/PC as a standard BLE keyboard (no app needed)
2. **HID keyboard** — sends the Space key when the single Action button is pressed
3. **Sleep & wake** — enters sleep mode when idle; wakes on button press and sends the keystroke
4. **Single button** — handles all actions: keystroke, bond clear, pairing, and wake (Config button removed)
5. **Three LEDs** — status dashboard: power, BLE, keystroke
6. **Aggressive three-tier power management** — full speed, auto light sleep, deep sleep (with reduced CPU frequencies)
7. **Consistent, timestamped debug logging** — with log levels and macros

## Hardware Overview

| Component     | Board Pin | GPIO    | Notes                                           |
| ------------- | --------- | ------- | ----------------------------------------------- |
| Red LED       | D5        | GPIO 7  | Boot / wake indicator (220 Ω)                   |
| Orange LED    | D3        | GPIO 38 | BLE connection status (220 Ω)                   |
| Yellow LED    | D2        | GPIO 3  | Keystroke confirmation (220 Ω)                  |
| Action Button | D6        | GPIO 18 | Single button: Space, bond clear, pairing, wake |

**Button wiring:**

- GPIO 18 to one button pin
- Diagonally opposite pin to 3V3
- 10kΩ pull-down resistor from GPIO to GND (same side as GPIO)
- Pressing the button connects GPIO to 3V3, reading HIGH

**LEDs:**

- Each LED: GPIO → 220Ω resistor → LED anode → GND

See [docs/layout.jpg](docs/layout.jpg) for the physical layout and wiring diagram.

## Power Management

| State               | Trigger                        | Estimated Current | BLE       | Wake          |
| ------------------- | ------------------------------ | ----------------- | --------- | ------------- |
| Full speed (80 MHz) | Active use                     | ~40–60 mA         | Connected | —             |
| Auto light sleep    | 10 s idle                      | ~5–15 mA          | Connected | Instant       |
| Deep sleep          | 30 s idle / 2 min disconnected | ~10–20 µA         | Off       | Action button |

_CPU frequencies have been reduced for better efficiency. Power management is now more aggressive and effective. Auto light sleep uses `esp_pm_configure()` so the CPU naps between BLE connection intervals. BLE stays connected — no reconnect delay on wake._

## LED States

| LED                | Meaning                    |
| ------------------ | -------------------------- |
| Red ON             | Board is powered           |
| Orange blinking    | Waiting for BLE connection |
| Orange solid       | BLE connected              |
| Yellow flash       | Keystroke sent             |
| Yellow rapid blink | Bond clear in progress     |

## Build & Upload

1. Install [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
2. Build: `pio run`
3. Upload: `pio run --target upload`

If the board isn't detected: the ESP32-S3 USB CDC only exists while firmware is running. If the board is stuck in deep sleep, enter bootloader mode: **hold BOOT → plug in USB → release BOOT**. Then upload or erase flash with:

```bash
esptool.py --port /dev/ttyACM0 erase_flash
```

The ESP32-S3 appears as `/dev/ttyACM0` or `/dev/ttyACM1`. The port number may change after a deep sleep reboot. Set `monitor_port` in `platformio.ini` if needed.

## Usage

1. Power on the board — **Red LED** turns on.
2. **Orange LED** blinks while advertising over BLE.
3. Pair from your phone/PC — look for **"S3-Pony-Keeb"** in Bluetooth settings.
4. **Orange LED** goes solid when connected.
5. Press the **Action button** — sends Space, **Yellow LED** flashes.
6. To re-pair: hold the **Action button** for 3 seconds to clear bonds and restart.

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

## Debug Logging

All debug output is timestamped and tagged by subsystem, with log levels for filtering. See `main.cpp` for the `DBG()` macro and usage examples. This makes troubleshooting and power state transitions much easier to follow.

## License

Unlicensed — personal project.
