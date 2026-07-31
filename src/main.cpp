// BLE Remote -> USB HID Bridge (Zwift Navigation Remote)
//
// The ESP32-S3 acts as a BLE central, reads the DQX-Q7 remote's report,
// and re-emits it over the native USB port as a TinyUSB HID keyboard.
// The button->key map is fixed at compile time in config.h.
//
// Pairing needs no hardcoded MAC address: the remote's advertisement has
// no identifying name/UUID/manufacturer data (see config.h), so on first
// boot the firmware connects to nearby candidates one at a time and keeps
// whichever one exposes the expected GATT service signature, saving its
// address to NVS. Every boot after that reconnects directly to the saved
// address. Send "forget" over the serial monitor to discard it and
// re-learn a (possibly different) remote.
//
// See project.md for the button map, known risks, and roadmap.

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <vector>

#include "config.h"

USBHIDKeyboard keyboard;
Preferences prefs;

static const char *kPrefsNamespace = "zwiftkeyer";
static const char *kPrefsAddrKey = "remoteaddr";

static NimBLEAddress targetAddress("");
static NimBLEClient *client = nullptr;
static volatile bool deviceFound = false;
static volatile bool connected = false;
static bool haveKnownAddress = false;
static std::vector<NimBLEAddress> rejectedAddresses;

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
  if (length == 2 && data[0] != 0) {
    handleButtonPress(data[0]);
  }
}

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *pClient) override {
    Serial.println("[BLE] disconnected, rescanning...");
    connected = false;
    deviceFound = false;
    NimBLEDevice::getScan()->start(0, false);
  }
};

static bool isRejected(const NimBLEAddress &address) {
  for (const auto &rejected : rejectedAddresses) {
    if (rejected.equals(address)) return true;
  }
  return false;
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *device) override {
    if (haveKnownAddress) {
      // Fast path: we already know this remote, ignore everything else.
      if (device->getAddress().toString() != targetAddress.toString()) return;
    } else {
      // Learning mode: only try candidates that are connectable, close by
      // (strong signal -- hold the remote near the board while learning),
      // and not already ruled out this session.
      if (!device->isConnectable()) return;
      if (device->haveRSSI() && device->getRSSI() < LEARNING_MIN_RSSI_DBM) {
        return;
      }
      if (isRejected(device->getAddress())) return;
    }

    Serial.printf("[BLE] candidate %s (RSSI %d), connecting to verify...\n",
                  device->getAddress().toString().c_str(),
                  device->haveRSSI() ? device->getRSSI() : 0);
    targetAddress = device->getAddress();
    deviceFound = true;
    NimBLEDevice::getScan()->stop();
  }
};

// Only the standard HID service actually carries button reports (the two
// proprietary JieLi services stay silent -- see project.md 3.2); narrowing
// the subscription to just this service avoids the Battery/Generic-
// Attribute/proprietary noise from subscribing to everything.
static bool subscribeToHidReports() {
  NimBLERemoteService *hidService =
      client->getService(NimBLEUUID(RemoteSignature::HID_SERVICE));
  if (hidService == nullptr) return false;

  auto *characteristics = hidService->getCharacteristics(true);
  for (auto *characteristic : *characteristics) {
    if (characteristic->canNotify() || characteristic->canIndicate()) {
      characteristic->subscribe(true, onNotify);
    }
  }
  return true;
}

// This exact combination of services (standard HID + both proprietary
// JieLi services) is what distinguishes the DQX-Q7 from any other nearby
// BLE device encountered while learning -- see config.h.
static bool matchesRemoteSignature() {
  return client->getService(NimBLEUUID(RemoteSignature::HID_SERVICE)) != nullptr &&
         client->getService(NimBLEUUID(RemoteSignature::JIELI_SERVICE_1)) != nullptr &&
         client->getService(NimBLEUUID(RemoteSignature::JIELI_SERVICE_2)) != nullptr;
}

static void connectToRemote() {
  Serial.println("[BLE] connecting...");

  // A fresh client per attempt -- NimBLE clients are meant for reconnecting
  // to the *same* peer, and reusing one across different candidate
  // addresses (as learning mode does) has led to a wedged/crashed radio in
  // testing. Recreating it every time costs a little overhead but avoids
  // any stale per-peer state carrying over between candidates.
  if (client != nullptr) {
    NimBLEDevice::deleteClient(client);
    client = nullptr;
  }
  client = NimBLEDevice::createClient();
  client->setClientCallbacks(new ClientCallbacks(), false);
  // Don't let one unresponsive candidate block learning mode for NimBLE's
  // 30s default -- see config.h.
  client->setConnectTimeout(LEARNING_CONNECT_TIMEOUT_S);

  if (!client->connect(targetAddress)) {
    Serial.println("[BLE] connect failed, will retry");
    if (!haveKnownAddress) rejectedAddresses.push_back(targetAddress);
    deviceFound = false;
    NimBLEDevice::getScan()->start(0, false);
    return;
  }

  if (!matchesRemoteSignature()) {
    Serial.println("[BLE] not a match, disconnecting and resuming scan");
    client->disconnect();
    if (!haveKnownAddress) rejectedAddresses.push_back(targetAddress);
    deviceFound = false;
    NimBLEDevice::getScan()->start(0, false);
    return;
  }

  if (!haveKnownAddress) {
    Serial.printf("[BLE] remote matched, saving %s to NVS\n",
                  targetAddress.toString().c_str());
    prefs.putString(kPrefsAddrKey, targetAddress.toString().c_str());
    haveKnownAddress = true;
  }

  Serial.println("[BLE] securing link...");
  client->secureConnection();
  subscribeToHidReports();
  connected = true;
  Serial.println("[BLE] ready");
}

static void forgetRemote() {
  Serial.println("[BLE] forgetting saved remote, will re-learn...");
  prefs.remove(kPrefsAddrKey);
  haveKnownAddress = false;
  rejectedAddresses.clear();
  connected = false;
  deviceFound = false;
  if (client != nullptr && client->isConnected()) {
    client->disconnect();
  }
  NimBLEDevice::getScan()->start(0, false);
}

static void pollSerialCommands() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      line.trim();
      if (line == "forget") forgetRemote();
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] BLE->USB HID bridge (Zwift Navigation Remote)");

  // --- USB HID keyboard (TinyUSB) ---
  keyboard.begin();
  USB.begin();

  // --- Saved remote address (NVS) ---
  prefs.begin(kPrefsNamespace, false);
  String saved =
      prefs.isKey(kPrefsAddrKey) ? prefs.getString(kPrefsAddrKey, "") : "";
  if (saved.length() > 0) {
    targetAddress = NimBLEAddress(saved.c_str());
    haveKnownAddress = true;
    Serial.printf("[BLE] known remote: %s\n", saved.c_str());
  } else {
    Serial.println("[BLE] no saved remote -- learning mode: will connect to "
                    "nearby candidates and keep the first one that matches");
  }

  // --- BLE central (NimBLE) ---
  NimBLEDevice::init("");
  // Bonding so we don't have to re-pair after every reconnect (the bond
  // key is persisted in NVS by the NimBLE stack). Only requested after a
  // candidate's service signature is verified -- see connectToRemote() --
  // so learning mode doesn't trigger a security prompt on unrelated nearby
  // devices it briefly connects to and rejects.
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false,
                                 /*sc=*/true);

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  scan->setActiveScan(true);
  scan->start(0, false);
}

void loop() {
  pollSerialCommands();

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
