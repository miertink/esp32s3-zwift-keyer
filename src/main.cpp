// BLE Remote -> USB HID Bridge (Zwift Navigation Remote)
//
// The ESP32-S3 acts as a BLE central, reads the DQX-Q7 remote's report,
// and re-emits it over the native USB port as a TinyUSB HID keyboard.
// The button->key map is fixed at compile time in config.h.
// See project.md for the button map, known risks, and roadmap.

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <NimBLEDevice.h>

#include "config.h"

USBHIDKeyboard keyboard;

static NimBLEAddress targetAddress("");
static NimBLEClient *client = nullptr;
static volatile bool deviceFound = false;
static volatile bool connected = false;

struct ButtonState {
  bool pending = false;
  uint32_t pendingSince = 0;
};
static ButtonState buttonStates[kButtonMapCount];

static void handleButtonPress(uint8_t mask) {
  for (size_t i = 0; i < kButtonMapCount; i++) {
    const ButtonMapping &mapping = kButtonMap[i];
    if (mapping.mask != mask) continue;

    if (mapping.doubleKey == 0) {
      // No double-tap configured for this button -> fire instantly.
      keyboard.write(mapping.singleKey);
      return;
    }

    ButtonState &state = buttonStates[i];
    uint32_t now = millis();
    if (state.pending && (now - state.pendingSince) <= DOUBLE_TAP_WINDOW_MS) {
      // Second tap inside the window -> double-tap action.
      state.pending = false;
      keyboard.write(mapping.doubleKey);
    } else {
      // First tap -> wait out the window in loop() for a possible second.
      state.pending = true;
      state.pendingSince = now;
    }
    return;
  }
}

// Solid green once the remote is paired; otherwise a short double-flash in
// blue while waiting for it (see the note on the single onboard LED in
// config.h). A fully dark LED means the firmware itself isn't running.
static void updateStatusLed() {
  if (connected) {
    neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);
    return;
  }

  constexpr uint32_t kPeriodMs =
      LED_FLASH_ON_MS * 2 + LED_FLASH_GAP_MS + LED_CYCLE_PAUSE_MS;
  uint32_t t = millis() % kPeriodMs;
  bool flashOn = (t < LED_FLASH_ON_MS) ||
                 (t >= LED_FLASH_ON_MS + LED_FLASH_GAP_MS &&
                  t < LED_FLASH_ON_MS * 2 + LED_FLASH_GAP_MS);
  if (flashOn) {
    neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);
  } else {
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
  }
}

// The DQX-Q7 report is a Consumer usage, 2 bytes, bitmap in byte 0: a tap
// arrives as "<mask> 00" immediately followed by a "00 00" release (see
// project.md 3.3-3.4), so only the non-zero byte-0 frame is a button press.
static void onNotify(NimBLERemoteCharacteristic *characteristic,
                      uint8_t *data, size_t length, bool isNotify) {
  Serial.printf("[BLE] notify %s:",
                characteristic->getUUID().toString().c_str());
  for (size_t i = 0; i < length; i++) Serial.printf(" %02X", data[i]);
  Serial.println();

  if (length == 2 && data[0] != 0) {
    handleButtonPress(data[0]);
  }
}

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *pClient) override {
    Serial.println("[BLE] connected, securing link...");
    pClient->secureConnection();
  }
  void onDisconnect(NimBLEClient *pClient) override {
    Serial.println("[BLE] disconnected, rescanning...");
    connected = false;
    deviceFound = false;
    NimBLEDevice::getScan()->start(0, false);
  }
};

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *device) override {
    if (device->getAddress().toString() == REMOTE_BLE_ADDRESS) {
      Serial.println("[BLE] DQX-Q7 found, stopping scan...");
      targetAddress = device->getAddress();
      deviceFound = true;
      NimBLEDevice::getScan()->stop();
    }
  }
};

// Subscribes to every notify/indicate characteristic on every service.
// TODO: once the real report characteristic is known from the logs above,
// subscribe only to that one instead of everything.
static void subscribeToAll() {
  auto *services = client->getServices(true);
  for (auto *service : *services) {
    auto *characteristics = service->getCharacteristics(true);
    for (auto *characteristic : *characteristics) {
      if (characteristic->canNotify() || characteristic->canIndicate()) {
        Serial.printf("[BLE] subscribing to %s (service %s)\n",
                      characteristic->getUUID().toString().c_str(),
                      service->getUUID().toString().c_str());
        characteristic->subscribe(true, onNotify);
      }
    }
  }
}

static void connectToRemote() {
  Serial.println("[BLE] connecting...");
  if (client == nullptr) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(new ClientCallbacks(), false);
  }

  if (!client->connect(targetAddress)) {
    Serial.println("[BLE] connect failed, will retry");
    deviceFound = false;
    NimBLEDevice::getScan()->start(0, false);
    return;
  }

  subscribeToAll();
  connected = true;
  Serial.println("[BLE] ready");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] BLE->USB HID bridge (Zwift Navigation Remote)");

  // --- USB HID keyboard (TinyUSB) ---
  keyboard.begin();
  USB.begin();

  // --- BLE central (NimBLE) ---
  NimBLEDevice::init("");
  // Bonding so we don't have to re-pair after every reconnect (the bond
  // key is persisted in NVS by the NimBLE stack).
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false,
                                 /*sc=*/true);

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  scan->setActiveScan(true);
  scan->start(0, false);
}

void loop() {
  if (deviceFound && !connected) {
    connectToRemote();
  }

  uint32_t now = millis();
  for (size_t i = 0; i < kButtonMapCount; i++) {
    ButtonState &state = buttonStates[i];
    if (state.pending && (now - state.pendingSince) > DOUBLE_TAP_WINDOW_MS) {
      // Window elapsed with no second tap -> single-tap action.
      state.pending = false;
      keyboard.write(kButtonMap[i].singleKey);
    }
  }

  updateStatusLed();
  delay(10);
}
