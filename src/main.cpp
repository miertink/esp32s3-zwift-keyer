// BLE Remote -> USB HID Bridge (Zwift Navigation Remote)
//
// Phase 1 (core, no Wi-Fi): the ESP32-S3 acts as a BLE central, reads the
// DQX-Q7 remote's report, and re-emits it over the native USB port as a
// TinyUSB HID keyboard.
// See project.md for the button map, known risks, and roadmap.

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <NimBLEDevice.h>

#include "config.h"

USBHIDKeyboard keyboard;

static NimBLEAdvertisedDevice *remoteDevice = nullptr;
static bool connected = false;

// TODO (Phase 1): report parser + single/double-tap state machine
// (see project.md sections 3.3-3.5 and 5). For now just logs the raw report.
static void onNotify(NimBLERemoteCharacteristic *characteristic,
                      uint8_t *data, size_t length, bool isNotify) {
  if (length < 1) return;
  Serial.printf("[BLE] report: %02X %02X\n", data[0],
                length > 1 ? data[1] : 0);
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *device) override {
    if (device->getAddress().toString() == REMOTE_BLE_ADDRESS) {
      Serial.println("[BLE] DQX-Q7 found, stopping scan...");
      NimBLEDevice::getScan()->stop();
    }
  }
};

static void connectToRemote() {
  // TODO (Phase 1): connect, pair/bond (persist in NVS), discover the
  // report service/characteristic, and subscribe to notifications.
  // See project.md section 7.2 about accessing HID 0x1812 as a central.
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] BLE->USB HID bridge (Zwift Navigation Remote)");

  // --- USB HID keyboard (TinyUSB) ---
  keyboard.begin();
  USB.begin();

  // --- BLE central (NimBLE) ---
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  scan->setActiveScan(true);
  scan->start(0, false);

  // TODO (Phase 1): LittleFS + /config.json parser (ArduinoJson).
  // TODO (Phase 1): name->HID keycode map.
}

void loop() {
  if (!connected) {
    // TODO (Phase 1): aggressive (re)connection logic after the remote
    // sleeps (see project.md section 7.3).
  }
  delay(10);
}
