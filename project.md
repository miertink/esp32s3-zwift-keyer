# BLE Remote → USB HID Bridge (Zwift Navigation Remote)

> Bridge that reads a BLE handlebar remote (DQX-Q7) and re-emits it to the PC
> as a USB HID keyboard, allowing navigation of the Zwift interface (arrows /
> Enter / Esc) — recovering on the PC the "cursor" experience that the Apple
> Remote gave on the Apple TV. In-game actions continue to be done via the
> Zwift Companion app.

---

## 1. Goal

I migrated Zwift from the Apple TV to the PC. On the Apple TV I navigated
the menus with the Apple Remote (D-pad + select). On the PC I lost that
interface "remote control".

This project turns a Bluetooth handlebar media remote into a
**USB navigation keyboard** for Zwift on the PC:

- **4 directions** (arrows) + **Enter** to navigate/confirm in menus.
- **Esc** to go back/exit (via double-tap).
- In-game actions (power-ups, camera, gestures, messages) **remain on the
  Zwift Companion**, which I've used for years without issues. They are not
  a target of this project.
- Double-tap is available as a bonus for future extra actions.

**Deliberately lean scope:** the remote = "Apple Remote for the PC".
Nothing beyond navigation is required.

---

## 2. Hardware

### Chosen board: ESP32-S3 (DevKit N16R8)
Reason: it's the only part on hand that has **BLE central** (to listen to
the remote) **and native USB** (to present itself to the PC as a real HID
keyboard), simultaneously.

Parts ruled out, and why:
- **ATmega32U4 (Pro Micro):** has native USB, but **zero Bluetooth**. Out.
- **ESP32-WROOM:** has BLE, but **no native USB device** (USB via a serial
  chip, CP2102/CH340). Doesn't become a clean USB keyboard. Out for this
  approach.
- **ESP32-C3:** has BLE, but USB is Serial/JTAG, not a comfortable full HID.
  Plan B if needed.
- **NodeMCU / ESP-01 (ESP8266):** no Bluetooth. Out.
- **JY-MCU BT_Board (HC-05/06):** Bluetooth Classic SPP, no BLE and no HID
  host. Out.

### The remote: DQX-Q7
- Waterproof Bluetooth handlebar/steering-wheel media remote, **5 buttons**.
- SoC manufacturer: **Zhuhai Jieli (JieLi)** — a common Chinese BT chip.
- Sold as a "Bluetooth Media Button Remote" for motorcycles/bikes (reseller
  brands: Jaesien, etc.). Works on iOS/Android with no app.
- **BT Address:** `98:4a:c0:ce:bf:a2`
- Original power source: **CR2** battery (non-rechargeable 3V lithium).

---

## 3. How the remote behaves (captured data)

### 3.1 BLE identity
- Shows up as **BLE** (confirmed in Device Manager as "Bluetooth LE
  Device"; and in scans with Appearance = HID Keyboard, `0x03C1`).
- **PnP ID = `02-AC-05-2C-02-1B-01`** → Vendor ID `0x05AC` = **Apple**.
  The remote deliberately spoofs an Apple identity, so iOS/tvOS treats it
  well.
- `ManufacturerName = zhuhai_jieli`, `ModelNumber = hid_mouse` (default
  firmware string — **ignore it**, it doesn't reflect actual behavior).

### 3.2 GATT services (6 services)
- `0x1800` Generic Access
- `0x1801` Generic Attribute
- `0x180A` Device Information
- `0x180F` Battery
- `0xAE40` (proprietary JieLi): char `0xAE41` (write), `0xAE42` (notify/CCCD)
- `0xAE00` (proprietary JieLi): char `0xAE01` (write), `0xAE02` (notify/CCCD)

> **There is NO HID `0x1812` service visible via generic GATT on iOS.**
> iOS hides HID from third-party apps. But the HID **does exist** and works:
> on Windows the remote operates as a native keyboard/consumer HID (volume,
> play, track skip work with no companion app). The proprietary `0xAExx`
> services, when subscribed (AE42/AE02), **emitted nothing** on button
> presses — the buttons come out over **standard HID**, not the AExx ones.

### 3.3 Button HID report (the central data) — captured via WebHID
Tool used: **USB Device Viewer online (WebHID)**
(https://www.codertools.net/tools/usb-device-viewer.php — Chrome/Edge only).

```
=== HID Collection ===
Usage Page: 0x000C (Consumer)
Usage:      0x0001 (Consumer Control)
Report ID:  1
Size:       2 bytes (bitmap)
```

**Button map (bitmap, byte 0):**

| Physical button   | Report value | Mask    | Bit |
|--------------------|:------------:|:-------:|:---:|
| Vol+               | `01 00`      | `0x01`  |  0  |
| Vol−               | `02 00`      | `0x02`  |  1  |
| → (forward)        | `04 00`      | `0x04`  |  2  |
| ← (back)           | `08 00`      | `0x08`  |  3  |
| Play/Pause         | `10 00`      | `0x10`  |  4  |

- Each tap sends `<value> 00` **immediately** followed by `00 00` (release).
- `00 00` = all buttons released (it's a positional bitmap, not "one usage at
  a time").
- "Is it pressed?" test: `if (report[0] & 0x10)` (Play/Pause example).

### 3.4 Per-button behavior (critical for firmware logic)
1. **All** buttons send a clean pulse `<value>` → `00 00`. The release
   arrives right away, **even while the button is still physically held
   down**.
2. **Decisive consequence:** the remote **does not expose press duration**.
   Therefore, **HOLD via time measurement is IMPOSSIBLE** on this remote.
   (Ruled out.)
3. **Vol+ / Vol−:** holding it down makes the remote's firmware auto-repeat
   — it sends `<value>`/`00 00` repeatedly (~4 Hz). This could be used as a
   "repeat" if desired, but it conflicts with double-tap detection (short
   window).
4. **← (back):** holding it for 1-2s opens the browser on the host, but
   **nothing shows up in the report** — this "hold" is intercepted by the
   remote's own firmware and goes out over a different channel (a different
   report ID, or AExx) invisible to WebHID. **Not usable** by this project.
   Ignore it; only use the ← tap.

### 3.5 Action expansion — decision
- **HOLD:** ruled out (hardware doesn't expose duration).
- **DOUBLE-TAP:** adopted. 100% firmware logic (counts taps of the same
  button within a window). Doesn't depend on the remote. Robust.
- **MULTI-PRESS (combos):** possible in theory (the bitmap allows 2 bits),
  **untested**. Only investigate if more slots are needed.

---

## 4. Architecture

```
┌──────────────┐   BLE (central)      ┌──────────────┐   USB HID (device)   ┌──────────┐
│  DQX-Q7      │ ───notify report───► │  ESP32-S3    │ ───keyboard report──►│   PC     │
│  (5 buttons) │  Consumer 0x0C 2B    │  (the bridge)│  arrows/enter/esc    │  Zwift   │
└──────────────┘                      └──────────────┘                      └──────────┘
        │                                    │
        │                              reads JSON config
        │                              (LittleFS)
        └── standard HID 0x1812              │
            (visible on Windows,             └── GPIO jumper -> web config mode (Phase 2)
             hidden on iOS)
```

- **Input:** ESP32-S3 as **BLE central**, subscribes to the remote's HID
  notifications, reads the 2-byte report, applies the bitmask.
- **Processing:** looks up the map (JSON on LittleFS), decides the key,
  applies single/double-tap logic.
- **Output:** **USB HID keyboard** (TinyUSB) — the PC sees a plain keyboard
  plugged in. Wired USB output = 100% supported by Zwift on PC (avoids BLE
  keyboard host limitations).

### Toolchain (confirmed)
- **Arduino-ESP32 (core 3.x)**
- **TinyUSB** (USB HID keyboard side)
- **NimBLE-Arduino** (BLE central side) — lightweight, coexists well with
  TinyUSB
- Storage: **LittleFS** (config JSON), **NVS** (BLE bond keys)

> Note: no ESP-IDF 4.4 constraint like the one in the ZX-Wespi project. Here
> the new stack (Arduino-ESP32 3.x) is used freely — it's where S3 + USB HID +
> BLE central are best supported.

---

## 5. Key map (initial config)

Philosophy: **Apple Remote-style navigation**. 4 instant direction buttons +
Enter (single) / Esc (double) on Play/Pause.

| Button       | Mask    | Single (instant)     | Double            |
|--------------|:-------:|-----------------------|-------------------|
| ← back       | `0x08`  | `LEFT`                | — (free)          |
| → forward    | `0x04`  | `RIGHT`               | — (free)          |
| Vol+         | `0x01`  | `UP`                  | — (free)          |
| Vol−         | `0x02`  | `DOWN`                | — (free)          |
| Play/Pause   | `0x10`  | `RETURN` (Enter)      | `ESCAPE` (back)   |

- **4 directions = instant** (no double-tap → no window latency → fluid
  navigation like the Apple Remote).
- **Only Play/Pause has double-tap** → it's the only one that pays the window
  latency (~300ms), which is imperceptible for Enter/Esc in a menu.
- Double-taps on the other buttons are left free for future expansion via
  JSON.

### Config file format (`/config.json` on LittleFS)

```json
{
  "double_tap_window_ms": 300,
  "buttons": {
    "back":       { "mask": "0x08", "single": "LEFT" },
    "forward":    { "mask": "0x04", "single": "RIGHT" },
    "vol_up":     { "mask": "0x01", "single": "UP" },
    "vol_down":   { "mask": "0x02", "single": "DOWN" },
    "play_pause": { "mask": "0x10", "single": "RETURN", "double": "ESCAPE" }
  }
}
```

Parser rules:
- Button **without** a `"double"` field → **instant** mode (fires `single`
  right on press, doesn't wait for the window).
- Button **with** a `"double"` field → waits `double_tap_window_ms`; 1 tap =
  `single`, 2 taps = `double`.
- Key names → map to HID keycodes (USB HID Usage Page 0x07 table). Support at
  least: `LEFT RIGHT UP DOWN RETURN ESCAPE SPACE TAB`, letters `A–Z`, digits
  `0–9`, `F1–F12`, `PAGEUP PAGEDOWN`.

---

## 6. Implementation roadmap

### Phase 1 — Functional core (NO Wi-Fi)
Goal: prove out BLE central + USB HID + remapping, file-based config.

- [ ] Bring-up: initialize TinyUSB (HID keyboard) **and** NimBLE central
      together, in the correct order (a point of attention — see §7).
- [ ] USB HID keyboard: standard keyboard descriptor; `sendKey(keycode)`
      function with press+release.
- [ ] BLE central: scan → connect to the DQX-Q7 (`98:4a:c0:ce:bf:a2`).
- [ ] Bond/pairing: pair and **persist the key in NVS** to reconnect without
      re-pairing.
- [ ] Discover the HID service / report characteristic; **subscribe to
      notifications**. (If HID `0x1812` isn't accessible as a central,
      investigate reading via the boot protocol, or the report WebHID saw —
      Consumer 0x0C, 2 bytes.)
- [ ] Report parser: read 2 bytes, apply masks `0x01`–`0x10`, detect the
      "pressed" transition (bit goes up) and "released" (`00 00`).
- [ ] Config: LittleFS + JSON parser (`ArduinoJson`) for `/config.json`.
- [ ] Per-button state machine:
      - **instant** mode → fires on press.
      - **double** mode → window timer; 1 vs 2 taps.
      - filter volume **auto-repeat** (the ~4Hz burst shouldn't turn into N
        taps).
- [ ] Name→HID keycode map.
- [ ] End-to-end test on Zwift PC (navigate a menu).

### Phase 2 — Web config (on-demand via GPIO)
Goal: edit the JSON without recompiling, without sacrificing the radio
during normal use.

- [ ] Read a **GPIO (jumper/button)** at boot:
      - inactive → normal mode, **Wi-Fi off** (radio 100% for BLE).
      - active → bring up an **AP** with a web page to edit `/config.json`.
- [ ] Web page: a form that reads/writes the JSON on LittleFS; save → reboot
      into normal mode.
- [ ] (Optional) validate the JSON on submit.

> The JSON is the **contract** between the two phases. Phase 2 only writes
> to that file; Phase 1's core doesn't change. Design Phase 1's loader ready
> from the start to re-read the file.

### Future / optional
- [ ] Expand actions via the free double-taps (edit the JSON).
- [ ] Test multi-press (2 simultaneous bits) if more slots are needed.
- [ ] Status indicator (LED): connected / disconnected / config mode.
- [ ] Deep sleep + wake on BLE activity (if it ever goes battery-powered).

---

## 7. Points of attention / known risks

1. **BLE central + USB HID coexistence on the S3.**
   This is the point that needs the most care. TinyUSB in USB HID mode +
   NimBLE central running together: pay attention to stack init order.
   Mapped territory, but not a "works on the first blink" kind of thing.
   Isolate the bring-up.

2. **Accessing HID as a central.**
   On iOS the HID `0x1812` is hidden, but that's iOS policy — as a
   **central**, the S3 should be able to talk to the remote's standard HID
   (the one Windows sees). If `0x1812` doesn't show up in discovery,
   alternatives: boot protocol host, or check whether the Consumer report
   (the same one WebHID saw) comes through an accessible characteristic.
   **Validate this early.**

3. **Reconnection after the remote sleeps (UX risk).**
   The DQX-Q7 sleeps to save battery and only starts advertising again once a
   key is pressed. The S3 (central) needs to detect the re-advertising and
   reconnect fast, keeping the bond. **Worst case: the first tap after
   sleep is lost** during reconnection ("press once to wake, then use"). This
   is the remote's own behavior; minimize it with aggressive reconnection +
   a saved bond.

4. **HOLD doesn't exist.** Already ruled out — don't try to reintroduce it.
   Press duration isn't observable on this hardware.

5. **Volume auto-repeat vs. double-tap.** If double-tap is ever enabled on
   the volume buttons, use a short window or the burst will turn into a false
   double-tap.

6. **Double-tap latency.** Inherent: a single tap on a button configured
   with `double` only fires after the window. That's why the direction
   buttons are instant (no double) and only Play/Pause pays the latency.

---

## 8. Quick reference (cheat sheet)

- **Remote:** DQX-Q7, BLE, `98:4a:c0:ce:bf:a2`, JieLi, spoofs an Apple VID.
- **Report:** Consumer (0x0C), Report ID 1, 2 bytes, bitmap.
  `Vol+=0x01 Vol-=0x02 →=0x04 ←=0x08 Play=0x10`, release=`00 00`.
- **Board:** ESP32-S3 N16R8. **Toolchain:** Arduino-ESP32 3.x + TinyUSB +
  NimBLE.
- **Output:** USB HID keyboard → Zwift PC.
- **Config:** `/config.json` on LittleFS. Phase 2: web via on-demand AP
  (GPIO).
- **Map:** ←→↑↓ for the 4 directions + Enter (single Play) / Esc (double
  Play).
- **No HOLD.** Double-tap to expand. Companion stays in charge of in-game
  actions.

---

## 9. Suggested libraries (verify versions in VS Code / PlatformIO)

- `Adafruit TinyUSB Library` (or the TinyUSB bundled in the Arduino-ESP32
  3.x core)
- `NimBLE-Arduino` (h2zero)
- `ArduinoJson` (v7)
- `LittleFS` (built into the ESP32 core)

> Recommended to use **PlatformIO** in VS Code (dependency management and
> S3 board support more predictable than the Arduino IDE). Board:
> `esp32-s3-devkitc-1` (adjust 16MB flash / 8MB PSRAM to match the N16R8).
> Enable USB CDC/HID in `platformio.ini` (TinyUSB `build_flags` and
> `board_build.arduino.usb_mode`).

---

## 10. DevKit S3 USB ports (bench / PlatformIO)

The DevKit S3 has **two** USB-C ports, with different roles that are **not**
interchangeable:

| Physical port          | Chip / path                  | Role                                              | On this machine        |
|-------------------------|-------------------------------|----------------------------------------------------|--------------------------|
| **UART**                | CH340/CP2102 converter → S3   | Flash firmware + serial monitor (`Serial.print`)   | **COM4**                |
| **Native USB (OTG)**    | GPIO19/20 direct to the S3    | The HID "keyboard" seen by the PC                  | **COM5** (see caveat)   |

### Device Manager signature
- **UART (COM4):** shows up under *Ports (COM & LPT)* as *"Silicon Labs
  CP210x"* or *"USB-SERIAL CH340"*. The converter's VID (`10C4` or `1A86`).
  **Stable** — always present, regardless of firmware.
- **Native USB (COM5):** a **chameleon**, it changes depending on the
  firmware:
  - Factory firmware / CDC enabled → *Ports (COM)* as *"USB Serial Device"*
    or *"JTAG/serial debug unit"*, **VID `303A`** (Espressif) = the S3's
    signature.
  - **With the project's HID firmware running → it LEAVES "COM Ports"** and
    shows up under *Keyboards* + *Human Interface Devices (HID)* as *"HID
    Keyboard Device"*. **In this mode COM5 stops existing as a COM port.**
  - Bootloader mode (BOOT+RESET) → generic USB `303A PID_0002`.

> ⚠️ **Rule of thumb:** the port number that matters for the build is
> **COM4 (UART)** — that's what you flash and log through, and it's stable.
> **COM5 is only a COM port while the native USB is in CDC mode**; as soon as
> the HID firmware comes up, it becomes a keyboard and disappears from the
> COM list. **Do not** use COM5 as `upload_port`.

### Recommended flashing flow
- **Flash + log:** always over **COM4 (UART)**. Leaves the native USB free
  for HID.
- **Test the keyboard:** plug the **native USB** into the PC → it enumerates
  as a keyboard.
- You can keep **both plugged in** at the same time during dev: flash/log on
  COM4, test HID on the native USB, no cable swapping.
- If you ever flash over the native USB and the PC can't find the port:
  **hold BOOT, press RESET, release BOOT** (download mode). The HID firmware
  "hijacks" the native USB, hence the manual bootloader.

### `platformio.ini` — starting points
```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

; --- Flashing and monitor over UART (COM4) ---
upload_port = COM4
monitor_port = COM4
monitor_speed = 115200

; --- Native USB as HID (TinyUSB), NOT as CDC ---
; The S3 has native USB-OTG; use TinyUSB mode for the HID keyboard.
board_build.arduino.usb_mode = 1              ; 1 = TinyUSB (enables HID)
board_build.arduino.usb_cdc_on_boot = 0       ; 0 = don't occupy native USB with CDC serial
                                              ; (log goes over UART/COM4, native USB stays free for HID)

; --- Flash 16MB / PSRAM 8MB (N16R8) ---
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=0

lib_deps =
    h2zero/NimBLE-Arduino
    bblanchon/ArduinoJson@^7
    ; TinyUSB: use the one built into the Arduino-ESP32 3.x core (via ARDUINO_USB_MODE)
    ; or adafruit/Adafruit TinyUSB Library if you choose that one instead
```

> Notes:
> - `usb_cdc_on_boot = 0` is what keeps **COM5 out of the way** — with no CDC
>   on the native USB, it stays dedicated to HID and all the logging goes
>   over COM4. If you'd also like a CDC serial over the native USB during
>   debugging, set it to `= 1`, but then CDC and HID coexist on the same USB
>   and enumeration gets more confusing.
> - Confirm the exact `default_16MB.csv` partition name available in the
>   installed core (it can vary). Adjust if the build complains.
