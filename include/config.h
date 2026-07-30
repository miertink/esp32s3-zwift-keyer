#pragma once

// BLE Remote -> USB HID Bridge (Zwift Navigation Remote)
// Fixed hardware/protocol constants and the fixed button->key map. There is
// no runtime/web config: everything below is compiled into the firmware.
// See project.md sections 2-5.

#include <cstddef>
#include <cstdint>
#include <USBHIDKeyboard.h>

// --- DQX-Q7 remote ---------------------------------------------------
#define REMOTE_BLE_ADDRESS "98:4a:c0:ce:bf:a2"

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

constexpr ButtonMapping kButtonMap[] = {
    {ButtonMask::BACK,       KEY_LEFT_ARROW,  KEY_F9},
    {ButtonMask::FORWARD,    KEY_RIGHT_ARROW, KEY_F3},
    {ButtonMask::VOL_UP,     KEY_UP_ARROW,    0},
    {ButtonMask::VOL_DOWN,   KEY_DOWN_ARROW,  0},
    {ButtonMask::PLAY_PAUSE, KEY_RETURN,      KEY_ESC},
};
constexpr size_t kButtonMapCount = sizeof(kButtonMap) / sizeof(kButtonMap[0]);
