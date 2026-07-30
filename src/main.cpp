// BLE Remote -> USB HID Bridge (Zwift Navigation Remote)
//
// Fase 1 (nucleo, sem Wi-Fi): ESP32-S3 como BLE central le o report do
// controle DQX-Q7 e reemite pelo USB nativo como teclado HID (TinyUSB).
// Ver project.md para o mapa de botoes, riscos conhecidos e roadmap.

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <NimBLEDevice.h>

#include "config.h"

USBHIDKeyboard keyboard;

static NimBLEAdvertisedDevice *remoteDevice = nullptr;
static bool connected = false;

// TODO (Fase 1): parser de report + maquina de estados single/double-tap
// (ver project.md secao 3.3-3.5 e 5). Por ora so loga o report bruto.
static void onNotify(NimBLERemoteCharacteristic *characteristic,
                      uint8_t *data, size_t length, bool isNotify) {
  if (length < 1) return;
  Serial.printf("[BLE] report: %02X %02X\n", data[0],
                length > 1 ? data[1] : 0);
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *device) override {
    if (device->getAddress().toString() == REMOTE_BLE_ADDRESS) {
      Serial.println("[BLE] DQX-Q7 encontrado, parando scan...");
      NimBLEDevice::getScan()->stop();
    }
  }
};

static void connectToRemote() {
  // TODO (Fase 1): conectar, parear/bond (persistir no NVS), descobrir
  // servico/characteristic de report e assinar notificacoes.
  // Ver project.md secao 7.2 sobre acesso ao HID 0x1812 como central.
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

  // TODO (Fase 1): LittleFS + parser de /config.json (ArduinoJson).
  // TODO (Fase 1): mapa nome->keycode HID.
}

void loop() {
  if (!connected) {
    // TODO (Fase 1): logica de (re)conexao agressiva apos sleep do controle
    // (ver project.md secao 7.3).
  }
  delay(10);
}
