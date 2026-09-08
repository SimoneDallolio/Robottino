#include "BLEConfigModule.h"
#include "NetworkManager.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "Config.h"

namespace {
  BLEServer* server = nullptr;
  BLECharacteristic* configCharacteristic = nullptr;
  bool active = false;

  class ConfigCallbacks final : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
      String payload = characteristic->getValue().c_str();
      if (payload.isEmpty()) {
        bleConfigNotify("{\"ok\":false,\"error\":\"payload vuoto\"}");
        return;
      }

      bool accepted = networkManagerApplyBlePayload(payload);
      bleConfigNotify(accepted
        ? "{\"ok\":true,\"message\":\"configurazione salvata\"}"
        : "{\"ok\":false,\"error\":\"PIN o dati non validi\"}");
    }
  };

  ConfigCallbacks callbacks;
}

void bleConfigBegin(const String& deviceName) {
  if (active) return;

  BLEDevice::init(deviceName.c_str());
  server = BLEDevice::createServer();
  BLEService* service = server->createService(BLE_SERVICE_UUID);
  configCharacteristic = service->createCharacteristic(
    BLE_CONFIG_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY
  );
  configCharacteristic->addDescriptor(new BLE2902());
  configCharacteristic->setCallbacks(&callbacks);
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  active = true;
  Serial.printf("[BLE] Advertising attivo: %s\n", deviceName.c_str());
}

void bleConfigLoop() {
}

void bleConfigStop() {
  if (!active) return;
  BLEDevice::getAdvertising()->stop();
  BLEDevice::deinit(true);
  server = nullptr;
  configCharacteristic = nullptr;
  active = false;
  Serial.println("[BLE] BLE disattivato");
}

bool bleConfigIsActive() {
  return active;
}

void bleConfigNotify(const String& message) {
  if (!active || configCharacteristic == nullptr) return;
  configCharacteristic->setValue(message.c_str());
  configCharacteristic->notify();
}
