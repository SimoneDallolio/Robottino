#include "BLEConfigModule.h"
#include "NetworkManager.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <ArduinoJson.h>
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
      StaticJsonDocument<96> request;
      deserializeJson(request, payload);
      String action = request["action"] | "";
      // Le richieste diagnostiche inviano gia una risposta JSON dettagliata
      // dal gestore di rete; non sovrascriverla con la conferma generica.
      if (action == "scanWifi" || action == "savedNetworks" || action == "logs" || action == "retryWifi" ||
          action == "saveWifi" || action == "deleteWifi" || action == "stopBle") return;
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
  networkManagerRecordLog(String("[BLE] Disponibile per la configurazione: ") + deviceName);
}

void bleConfigStop() {
  if (!active) return;
  BLEDevice::getAdvertising()->stop();
  // Non rilasciare la memoria del controller: il BLE deve poter essere
  // riavviato con la pressione prolungata senza riavviare l'ESP32.
  BLEDevice::deinit(false);
  server = nullptr;
  configCharacteristic = nullptr;
  statusCharacteristic = nullptr;
  active = false;
  networkManagerRecordLog("[BLE] Configurazione BLE disattivata");
}

void bleConfigNotify(const String& message) {
  if (!active || configCharacteristic == nullptr) return;
  configCharacteristic->setValue(message.c_str());
  configCharacteristic->notify();
}
