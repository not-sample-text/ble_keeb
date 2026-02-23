# S3-Pony-Keeb — Specifications

## Objective

Build a BLE HID keyboard on the **DFRobot FireBeetle 2 ESP32-S3**.

## Requirements

1. **Bluetooth pairing** — pair with a phone over BLE
2. **HID keyboard** — ESP32-S3 acts as a Bluetooth keyboard (HID device) and sends the Space key when a button is pressed
3. **Sleep mode** — put the ESP32-S3 to sleep; on button press, wake up and then send Space over Bluetooth

## Development Extras

The following were added during development for debugging and ease of use — not part of the original spec:

- **3 LEDs** (red, orange, yellow) as a status dashboard (boot, BLE status, keystroke confirmation)
- **Single button** for all actions: keystroke, bond clear, pairing, and wake (Config button removed)
- **Improved three-tier power management** (lower CPU frequencies, more aggressive sleep)
- **Consistent, timestamped debug logging** via USB CDC, with log levels and macros

## Hardware

### Microcontroller

- **DFRobot FireBeetle 2 ESP32-S3**

### Pin Mapping

| Component     | Board Pin | GPIO    | Details                                                     |
| ------------- | --------- | ------- | ----------------------------------------------------------- |
| Red LED       | D5        | GPIO 7  | Boot / wake indicator, 220 Ω series resistor                |
| Orange LED    | D3        | GPIO 38 | BLE connection status, 220 Ω series resistor                |
| Yellow LED    | D2        | GPIO 3  | Keystroke confirmation, 220 Ω series resistor               |
| Action Button | D6        | GPIO 18 | Single button: Space, bond clear, pairing, wake (see below) |

### Layout

![Keyboard layout and wiring diagram](layout.jpg)

<div align="center"><em>Physical layout and wiring diagram</em></div>

### Wiring Notes

- **LEDs** connect from GPIO → 220 Ω resistor → GND. Driven HIGH = ON.
- **Single button (active-high):**
  - GPIO to one pin
  - Diagonally opposite pin to 3V3
  - 10kΩ pull-down resistor from GPIO to GND (same side as GPIO)
  - Other two pins (same side as first two) can be left unconnected or used for mechanical stability
- Pressing the button connects the GPIO to 3V3, reading **HIGH**. (Config button removed.)

## Software

- **PlatformIO** with Arduino framework on `espressif32`
- **NimBLE-Arduino** for the BLE stack
- **ESP32 BLE Keyboard** for the HID keyboard profile
- **Debug logging:** All debug output is timestamped and tagged by subsystem, with log levels for filtering. See `main.cpp` for the `DBG()` macro and usage examples.

# Power Management

Three-tier power management is now more efficient:

| State               | Trigger                        | Estimated Current | BLE       | Wake          |
| ------------------- | ------------------------------ | ----------------- | --------- | ------------- |
| Full speed (80 MHz) | Active use                     | ~40–60 mA         | Connected | —             |
| Auto light sleep    | 10 s idle                      | ~5–15 mA          | Connected | Instant       |
| Deep sleep          | 30 s idle / 2 min disconnected | ~10–20 µA         | Off       | Action button |

_CPU frequencies have been reduced for better efficiency. Power management is now more aggressive and effective._

# Button Functions

The single button (GPIO 18) now handles all actions:

| Gesture         | Action                            |
| --------------- | --------------------------------- |
| Short press     | Send Space keystroke (HID)        |
| Long press (3s) | Clear Bluetooth bonds and restart |
| Hold at boot    | Enter pairing/bond-clear mode     |
| Press in sleep  | Wake from deep sleep              |
