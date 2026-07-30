#pragma once

// BLE Remote -> USB HID Bridge (Zwift Navigation Remote)
// Constantes fixas do hardware / protocolo. Ver project.md secoes 2-5.

#include <cstdint>

// --- Controle DQX-Q7 ---------------------------------------------------
#define REMOTE_BLE_ADDRESS "98:4a:c0:ce:bf:a2"

// Report HID: Consumer (0x0C), Report ID 1, 2 bytes, bitmap no byte 0.
namespace ButtonMask {
constexpr uint8_t VOL_UP     = 0x01;
constexpr uint8_t VOL_DOWN   = 0x02;
constexpr uint8_t FORWARD    = 0x04;
constexpr uint8_t BACK       = 0x08;
constexpr uint8_t PLAY_PAUSE = 0x10;
}  // namespace ButtonMask

// --- Config (LittleFS) ---------------------------------------------------
#define CONFIG_PATH "/config.json"
#define DEFAULT_DOUBLE_TAP_WINDOW_MS 300
