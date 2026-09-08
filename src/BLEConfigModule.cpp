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
  BLECharacteristic* statusCharacteristic = nullptr;
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
        : String("{\"ok\":false,\"error\":\"") + networkManagerLastConfigError() + "\"}" );
    }
  };

  ConfigCallbacks callbacks;

  class StatusCallbacks final : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* characteristic) override {
      String status = networkManagerProvisioningStatus();
      characteristic->setValue(status.c_str());
    }
  };

  StatusCallbacks statusCallbacks;
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
  statusCharacteristic = service->createCharacteristic(
    BLE_STATUS_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ
  );
  statusCharacteristic->setCallbacks(&statusCallbacks);
  statusCharacteristic->setValue(networkManagerProvisioningStatus().c_str());
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
  statusCharacteristic = nullptr;
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
