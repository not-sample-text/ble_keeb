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
- **Config button** for clearing Bluetooth bonds and re-pairing
- **Three-tier power management** (full speed → auto light sleep → deep sleep) instead of simple deep sleep
- **Serial debug logging** via USB CDC

## Hardware

### Microcontroller

- **DFRobot FireBeetle 2 ESP32-S3**

### Pin Mapping

| Component     | Board Pin | GPIO    | Details                                       |
| ------------- | --------- | ------- | --------------------------------------------- |
| Red LED       | D5        | GPIO 7  | Boot / wake indicator, 220 Ω series resistor  |
| Orange LED    | D3        | GPIO 38 | BLE connection status, 220 Ω series resistor  |
| Yellow LED    | D2        | GPIO 3  | Keystroke confirmation, 220 Ω series resistor |
| Action Button | D6        | GPIO 18 | Button to 3V3 through 10 kΩ resistor          |
| Config Button | D7        | GPIO 9  | Button to 3V3 through 10 kΩ resistor          |

### Wiring Notes

- **LEDs** connect from GPIO → 220 Ω resistor → GND. Driven HIGH = ON.
- **Buttons** have one pin wired to the GPIO and the diagonal pin wired to 3V3 through a 10 kΩ resistor. Pressing the button connects the GPIO to 3V3, reading **HIGH**.

## Software

- **PlatformIO** with Arduino framework on `espressif32`
- **NimBLE-Arduino** for the BLE stack
- **ESP32 BLE Keyboard** for the HID keyboard profile
