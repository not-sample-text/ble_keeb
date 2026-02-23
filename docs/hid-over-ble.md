# HID over BLE — How & Why

![Keyboard layout and wiring diagram](layout.jpg)

<div align="center"><em>Physical layout and wiring diagram</em></div>

This document explains what HID is, how it works over Bluetooth Low Energy, what libraries we used, and the implementation details.

## What Is HID?

**HID** stands for **Human Interface Device**. It's a USB/Bluetooth protocol that lets peripherals like keyboards, mice, and game controllers communicate with a host (phone, PC, tablet) using a standardized format.

The key property: **no drivers or apps needed**. Any OS that supports HID will recognize the device natively — just pair and use.

> **References:**
>
> - [USB HID specification (usb.org)](https://www.usb.org/hid)
> - [Bluetooth HID Profile spec (bluetooth.com)](https://www.bluetooth.com/specifications/specs/human-interface-device-profile-1-1/)
> - [Introduction to BLE HID (Espressif)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/bluetooth/esp_hidd.html)

### How HID Works

An HID device sends **reports** to the host. A report is a small binary packet that describes input state — which keys are pressed, how far the mouse moved, etc.

The device declares its capabilities via a **Report Descriptor** — a binary blob that tells the host "I'm a keyboard with these keys" or "I'm a mouse with X/Y axes and 3 buttons." The host parses this once during pairing and then knows how to interpret every report.

For a keyboard, a typical report contains:

- **Modifier byte** — Shift, Ctrl, Alt, GUI flags
- **Reserved byte**
- **6 key slots** — up to 6 simultaneous key codes (USB HID keycodes, not ASCII)

```
Byte 0: Modifiers (bit flags)
Byte 1: Reserved (0x00)
Bytes 2-7: Keycodes (up to 6 keys)
```

> [USB HID Usage Tables (key codes)](https://usb.org/sites/default/files/hut1_4.pdf)

---

## HID over BLE

Classic Bluetooth HID (like older wireless keyboards) uses the **HID Profile (HID over BR/EDR)**. We use **HID over GATT (HOGP)** — the Bluetooth Low Energy version.

### HOGP — HID over GATT Profile

HOGP defines how HID reports are sent over BLE's **GATT** (Generic Attribute Profile) layer:

| GATT Component               | Role                                                       |
| ---------------------------- | ---------------------------------------------------------- |
| **HID Service** (0x1812)     | Contains the report descriptor and report characteristics  |
| **Report Characteristic**    | The actual input/output data (keyboard reports go here)    |
| **Report Map**               | The report descriptor — tells the host what this device is |
| **HID Information**          | HID version, country code                                  |
| **Battery Service** (0x180F) | Optional — reports battery level to the host               |

The phone subscribes to **notifications** on the Report Characteristic. When we press a key, the ESP32-S3 writes a new report value and the phone gets notified instantly.

> **References:**
>
> - [HOGP spec (bluetooth.com)](https://www.bluetooth.com/specifications/specs/hid-over-gatt-profile-1-0/)
> - [GATT overview (bluetooth.com)](https://www.bluetooth.com/specifications/specs/core-specification-amended-5-4/)

---

## What We Used

### Libraries

| Library                                                                 | Purpose                                                                                                        |
| ----------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| [ESP32 BLE Keyboard](https://github.com/T-vK/ESP32-BLE-Keyboard) (t-vk) | High-level HID keyboard API — handles the report descriptor, GATT service setup, and report sending            |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (h2zero)     | Lightweight BLE stack — replaces the default Bluedroid stack, uses less RAM (~50 KB vs ~170 KB) and less power |

The `ESP32 BLE Keyboard` library supports NimBLE as a backend when compiled with `-D USE_NIMBLE`.

### Why NimBLE Instead of Bluedroid?

|                   | Bluedroid (default)            | NimBLE                |
| ----------------- | ------------------------------ | --------------------- |
| RAM usage         | ~170 KB                        | ~50 KB                |
| Flash usage       | Larger                         | Smaller               |
| Power             | Higher                         | Lower                 |
| API               | ESP-IDF native                 | Simpler Arduino-style |
| BLE-only projects | Overkill (includes classic BT) | Purpose-built for BLE |

For a single-button keyboard on an ESP32-S3 with sleep modes, NimBLE is the obvious choice.

> [NimBLE-Arduino GitHub](https://github.com/h2zero/NimBLE-Arduino)

---

## How We Use It

### Initialization

```cpp
#include <BleKeyboard.h>
#include <NimBLEDevice.h>

BleKeyboard bleKeyboard("S3-Pony-Keeb", "TestTest", 100);
//                       ^ device name    ^ manufacturer  ^ battery level

void setup() {
    bleKeyboard.begin();
    // Library sets up:
    // - HID Service (0x1812)
    // - Report Map (keyboard descriptor)
    // - Report Characteristic (for key reports)
    // - Device Information Service
    // - Battery Service
    // - Starts BLE advertising
}
```

The device name appears in Bluetooth settings on the phone. BLE HID names are limited to **15 characters** (we learned this the hard way — "S3-Pony-Keyboard" got truncated).

### Checking Connection

```cpp
if (bleKeyboard.isConnected()) {
    // safe to send keystrokes
}
```

The library handles pairing, bonding, and reconnection. Once paired, the phone will auto-reconnect when the device advertises.

### Sending a Keystroke

We send Space when the Action button is pressed:

```cpp
bleKeyboard.press(' ');
delay(50);
bleKeyboard.releaseAll();
```

**Why `press()` + `releaseAll()` instead of `print()`?**

`bleKeyboard.print(" ")` sends a press and release in quick succession. We found it sometimes failed to register on the phone — the report was sent too fast or the host missed it. Separating press and release with a 50 ms delay gives the host time to process the key-down report before the key-up arrives.

Under the hood, `press()` does:

1. Sets the Space keycode (0x2C) in the 6-key report
2. Writes the report to the GATT Report Characteristic
3. BLE notifies the phone

`releaseAll()` sends a report with all zeros — all keys released.

### Bond Clearing

When the user holds the Config button for 3 seconds:

```cpp
NimBLEDevice::deleteAllBonds();
ESP.restart();
```

This removes all stored pairing keys. On restart, the device starts advertising as a new keyboard and the phone can pair fresh.

---

## The BLE HID Connection Flow

```
ESP32-S3                              Phone
   │                                    │
   ├── BLE advertising ──────────────►  │
   │   (name: "S3-Pony-Keeb")           │
   │                                    │
   │  ◄──────────── Connection request ─┤
   │                                    │
   ├── Pairing / Bonding ◄────────────► │
   │   (keys exchanged & stored)        │
   │                                    │
   ├── GATT Discovery ◄──────────────── │
   │   (phone finds HID Service)        │
   │                                    │
   ├── Report Map read ◄─────────────── │
   │   (phone learns: "this is a        │
   │    keyboard with these keys")      │
   │                                    │
   │   ── Subscribe to notifications ──►│
   │                                    │
   │        ╔══════════════╗            │
   │        ║  Connected!  ║            │
   │        ╚══════════════╝            │
   │                                    │
   ├── Key report (Space down) ───────► │  ← button pressed
   │                                    │
   ├── Key report (all released) ─────► │  ← 50ms later
   │                                    │
```

After the first pairing, reconnection skips the pairing/bonding step — the phone already has the keys stored. This makes reconnection faster (~1–2 s vs 3–8 s for first pair).

---

## Key Takeaways

| Lesson                                 | Detail                                                                              |
| -------------------------------------- | ----------------------------------------------------------------------------------- |
| HID = no app needed                    | The OS handles everything — just pair like a normal keyboard                        |
| HOGP uses GATT notifications           | Key reports are pushed to the phone as BLE notifications                            |
| `press()` + `releaseAll()` > `print()` | Explicit press/release with a delay is more reliable                                |
| NimBLE over Bluedroid                  | Half the RAM, less power, better for BLE-only projects                              |
| 15-char name limit                     | BLE HID device names get truncated beyond 15 characters                             |
| Bond clearing needs restart            | After deleting bonds, restart so the device re-advertises cleanly                   |
| Auto-reconnect works                   | Once paired, the phone reconnects automatically when it sees the device advertising |

## Further Reading

- [USB HID Specification](https://www.usb.org/hid)
- [USB HID Usage Tables (key codes)](https://usb.org/sites/default/files/hut1_4.pdf)
- [Bluetooth HOGP Specification](https://www.bluetooth.com/specifications/specs/hid-over-gatt-profile-1-0/)
- [ESP-IDF BLE HID Device API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/bluetooth/esp_hidd.html)
- [ESP32 BLE Keyboard library (T-vK)](https://github.com/T-vK/ESP32-BLE-Keyboard)
- [NimBLE-Arduino (h2zero)](https://github.com/h2zero/NimBLE-Arduino)
- [Bluetooth Core Specification](https://www.bluetooth.com/specifications/specs/core-specification-amended-5-4/)
