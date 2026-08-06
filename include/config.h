#pragma once

// BLE Remote -> USB HID Bridge (Zwift Navigation Remote)
// Fixed hardware/protocol constants and the fixed button->key map. There is
// no runtime/web config: everything below is compiled into the firmware.
// See project.md sections 2-5.

#include <cstddef>
#include <cstdint>
#include <USBHIDKeyboard.h>

// --- DQX-Q7 remote signature ---------------------------------------------
// The remote's BLE advertisement carries no name, no advertised service
// UUIDs, and no manufacturer data (confirmed empirically), so there is
// nothing to filter on before connecting. Instead, the firmware connects
// to whatever nearby device is advertising and checks, after connecting,
// whether it exposes this exact combination of GATT services -- specific
// to this remote's chipset. First match is saved to NVS so only the first
// boot ever needs to "learn" a remote; see main.cpp.
namespace RemoteSignature {
constexpr uint16_t HID_SERVICE     = 0x1812;  // standard HID-over-GATT
constexpr uint16_t JIELI_SERVICE_1 = 0xAE40;  // proprietary (write/notify)
constexpr uint16_t JIELI_SERVICE_2 = 0xAE00;  // proprietary (write/notify)
}  // namespace RemoteSignature

// Learning mode connects to nearby candidates one at a time to check their
// service signature. Two guards keep that from being slow/disruptive in a
// BLE-crowded room: only try candidates with a reasonably strong signal,
// and give up quickly on any single candidate that doesn't respond, instead
// of blocking for NimBLE's 30s default.
//
// The remote itself only reaches about -76dBm even held right next to the
// board (it's coin-cell powered, low BLE TX power by design) -- confirmed
// via the [BLE][RAW] diagnostic log in ScanCallbacks::onResult. -60 excluded
// it entirely; -80 leaves enough margin to include it while still excluding
// clearly-distant devices (the ones seen around -90 to -99dBm).
constexpr int8_t LEARNING_MIN_RSSI_DBM = -80;
// 1s is the minimum this API supports (it only takes whole seconds) -- as
// fast as possible, so a bad candidate wastes as little time as possible.
constexpr uint8_t LEARNING_CONNECT_TIMEOUT_S = 1;

// HID report: Consumer (0x0C), Report ID 1, 2 bytes, bitmap in byte 0.
namespace ButtonMask {
constexpr uint8_t VOL_UP     = 0x01;
constexpr uint8_t VOL_DOWN   = 0x02;
constexpr uint8_t FORWARD    = 0x04;
constexpr uint8_t BACK       = 0x08;
constexpr uint8_t PLAY_PAUSE = 0x10;
}  // namespace ButtonMask

// --- Button -> key map -------------------------------------------------
// doubleKey == 0 means the button has no double-tap action: it fires
// singleKey instantly on press, without waiting out the double-tap window.
struct ButtonMapping {
  uint8_t mask;
  uint8_t singleKey;
  uint8_t doubleKey;
};

constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 300;

// keyboard.write() presses and releases a key back-to-back with no gap at
// all, which some games miss: their input polling can run at a coarser
// interval than the press, so a same-frame down+up occasionally never gets
// sampled while it's "down". Holding the key for a short, human-scale
// duration before releasing it fixes that at the cost of a small, one-time
// blocking delay per keypress in loop().
constexpr uint32_t KEY_HOLD_MS = 50;

constexpr ButtonMapping kButtonMap[] = {
    {ButtonMask::BACK,       KEY_LEFT_ARROW,  KEY_F10},
    {ButtonMask::FORWARD,    KEY_RIGHT_ARROW, KEY_F3},
    {ButtonMask::VOL_UP,     KEY_UP_ARROW,    0},
    {ButtonMask::VOL_DOWN,   KEY_DOWN_ARROW,  0},
    {ButtonMask::PLAY_PAUSE, KEY_RETURN,      KEY_ESC},
};
constexpr size_t kButtonMapCount = sizeof(kButtonMap) / sizeof(kButtonMap[0]);

// --- Status LED ----------------------------------------------------------
// The board has a single onboard addressable RGB LED (RGB_BUILTIN, GPIO48),
// not two separate ones, so it does double duty as both indicators: it
// blinks a short double-flash while the firmware is running but the remote
// isn't paired yet, and turns solid once it is. If it's fully dark, the
// firmware isn't running (crashed or unplugged).
constexpr uint32_t LED_FLASH_ON_MS    = 80;
constexpr uint32_t LED_FLASH_GAP_MS   = 120;
constexpr uint32_t LED_CYCLE_PAUSE_MS = 700;
