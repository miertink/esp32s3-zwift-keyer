# BLE Remote -> USB HID Bridge (Zwift Navigation Remote)

This turns a cheap Bluetooth handlebar remote (the **DQX-Q7**, 5 buttons)
into a USB keyboard for your PC, so you can navigate the Zwift menus
(arrows / Enter / Esc) without touching the keyboard or mouse — the same
way the old Apple Remote worked on the Apple TV.

An ESP32-S3 board sits in the middle: it listens to the remote over
Bluetooth (BLE) on one side, and plugs into your PC over a USB cable on the
other side, where it shows up as an ordinary USB keyboard. No Bluetooth
pairing is needed on the PC — as far as Windows is concerned, it's just a
keyboard.

The full technical write-up (hardware choice, Bluetooth protocol details,
known quirks) is in [project.md](project.md); this README is the
practical "how do I actually use this" guide.

## What you need

- The ESP32-S3 board (DevKit N16R8), with **two USB-C cables** plugged into
  your PC (see "The two USB ports" below — you need both while setting up).
- The DQX-Q7 remote (the one with BT address `98:4a:c0:ce:bf:a2` in
  `include/config.h` — see "Using a different/replacement remote" if yours
  is a different unit).
- [VS Code](https://code.visualstudio.com/) with the **PlatformIO IDE**
  extension installed (search for "PlatformIO IDE" in the Extensions
  panel). PlatformIO handles downloading the right compiler and libraries
  for you — you don't need to install anything else by hand.

## The two USB ports

The board has two USB-C ports and they do different jobs:

| Port | What it's for |
|------|----------------|
| **UART** (labeled on the board, this machine's COM4) | Flashing the firmware and reading the debug log |
| **Native USB** | The one that becomes the "keyboard" your PC sees |

Keep both plugged in while you're setting this up. Once everything works,
you technically only need the native USB port connected for daily use, but
there's no harm in leaving both in.

## First-time setup (flashing the firmware)

1. Open this project folder in VS Code (`File > Open Folder...`). The
   PlatformIO extension will recognize it automatically (you'll see a
   little PlatformIO icon appear in the sidebar, and a blue status bar at
   the bottom).
2. In that blue status bar, click the **checkmark icon** (Build) first, to
   make sure it compiles.
3. Click the **right-arrow icon** (Upload) to flash it onto the board over
   the UART port.
4. Optional: click the **plug icon** (Serial Monitor) to watch the board's
   log — useful for confirming the remote connected (see next section).

If you prefer the terminal instead of the VS Code buttons:

```sh
pio run                 # compile
pio run -t upload       # flash over UART (COM4)
pio device monitor      # serial log (COM4, 115200)
```

## Connecting the remote (first-time pairing)

You don't need to do anything on the PC — no Bluetooth settings, no
pairing dialog. All the Bluetooth pairing happens between the ESP32 board
and the remote, and it's automatic:

1. Make sure the firmware is flashed and the board's native USB is plugged
   into your PC (it powers the board).
2. **Press any button on the DQX-Q7 remote.** These remotes go to sleep to
   save battery, and only start advertising over Bluetooth again once a
   button is pressed — so this step wakes it up and makes it visible to
   the board.
3. Within a couple of seconds, the board finds it (by its fixed Bluetooth
   address) and connects and pairs with it automatically. If you have the
   serial monitor open, you'll see:
   ```
   [BLE] DQX-Q7 found, stopping scan...
   [BLE] connected, securing link...
   [BLE] ready
   ```
4. That's it — press a button and the mapped key should show up wherever
   your cursor/focus is on the PC (try it in a text editor first, then in
   Zwift).

**You only need to do this once.** The board remembers the remote (the
pairing is saved in its internal flash) across power cycles and firmware
updates — you won't need to re-pair unless you erase the board's flash
entirely (`pio run -t erase`), or replace the remote with a different unit.

If the remote doesn't connect: press a button on it again (it may have
gone back to sleep), and check that `include/config.h` has the right
Bluetooth address for your unit (see below).

## Customizing which key each button sends

Open [include/config.h](include/config.h). The part you'll want to edit is
this table:

```cpp
constexpr ButtonMapping kButtonMap[] = {
    {ButtonMask::BACK,       KEY_LEFT_ARROW,  KEY_F9},
    {ButtonMask::FORWARD,    KEY_RIGHT_ARROW, KEY_F3},
    {ButtonMask::VOL_UP,     KEY_UP_ARROW,    0},
    {ButtonMask::VOL_DOWN,   KEY_DOWN_ARROW,  0},
    {ButtonMask::PLAY_PAUSE, KEY_RETURN,      KEY_ESC},
};
```

Each line is one physical button, with 3 columns:

1. **Button** (`ButtonMask::...`) — which physical button this row is for.
   Don't change this column; the five names correspond to the remote's
   five physical buttons (see project.md section 3 if you're curious about
   the underlying Bluetooth bitmask).
2. **Single-tap key** — the key sent immediately when you tap that button
   once.
3. **Double-tap key** — the key sent when you tap that button *twice*
   quickly. Use **`0`** here if you don't want a double-tap action for
   that button — it makes the button fire instantly on every press instead
   of waiting to see if a second tap is coming (that's why the four
   direction buttons feel instant: they have `0` in this column, so
   there's no wait).

"Quickly" is defined by this constant, also in `config.h` (in
milliseconds):

```cpp
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 300;
```

Raise it if double-taps feel too hard to land, lower it if single taps
feel laggy on buttons that have a double-tap action configured.

### What can I put in the key columns?

- **Letters, digits, and symbols**: just use the character directly, e.g.
  `'a'`, `'Z'`, `'5'`, `' '` (space).
- **Special keys**: use one of these names (defined by the keyboard
  library):

  | Name | Key |
  |------|-----|
  | `KEY_UP_ARROW`, `KEY_DOWN_ARROW`, `KEY_LEFT_ARROW`, `KEY_RIGHT_ARROW` | Arrow keys |
  | `KEY_RETURN` | Enter |
  | `KEY_ESC` | Escape |
  | `KEY_TAB` | Tab |
  | `KEY_BACKSPACE`, `KEY_DELETE` | Backspace / Delete |
  | `KEY_HOME`, `KEY_END` | Home / End |
  | `KEY_PAGE_UP`, `KEY_PAGE_DOWN` | Page Up / Page Down |
  | `KEY_F1` ... `KEY_F12` | Function keys |

After editing, save the file and re-flash (Upload button, or
`pio run -t upload`) for the change to take effect. You do **not** need to
re-pair the remote after reflashing.

## Using a different/replacement remote

`include/config.h` has the exact Bluetooth address of one specific DQX-Q7
unit hardcoded:

```cpp
#define REMOTE_BLE_ADDRESS "98:4a:c0:ce:bf:a2"
```

The board only ever looks for that one address, so a different physical
remote (a spare, a replacement after this one breaks, etc.) won't connect
until you update this line with its own address. To find it:

1. Install a BLE scanner app on your phone (e.g. **nRF Connect for
   Mobile**, free on iOS/Android).
2. Press a button on the new remote to wake it up, then scan for nearby
   Bluetooth devices in the app.
3. Look for a device advertising as a keyboard/HID device (it may show an
   Apple-looking name/vendor, since these remotes spoof an Apple identity
   — see project.md section 3.1) and note its address.
4. Put that address into `REMOTE_BLE_ADDRESS`, save, and re-flash.

## Troubleshooting

- **Upload fails with "port busy" / "access denied" on COM4**: close any
  serial monitor window that's already open (VS Code's included, or a
  separate PlatformIO monitor tab) — only one program can use the port at
  a time.
- **Remote won't connect**: press a button on it to wake it up; check the
  serial monitor log for `[BLE] ...` lines to see how far it gets.
- **Board acts strange after many firmware updates**: a full
  `pio run -t erase` followed by a fresh upload wipes everything, including
  the saved remote pairing — you'll need to redo the "first-time pairing"
  steps above afterward.
