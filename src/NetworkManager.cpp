#include "NetworkManager.h"
#include "BLEConfigModule.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

namespace {
  constexpr uint32_t CONNECT_TIMEOUT_MS = 12000;
  constexpr char DEFAULT_PIN[] = "0000";
  constexpr float DEFAULT_LATITUDE = 44.6471f;
  constexpr float DEFAULT_LONGITUDE = 10.9252f;
  constexpr float MIN_LATITUDE = -90.0f;
  constexpr float MAX_LATITUDE = 90.0f;
  constexpr float MIN_LONGITUDE = -180.0f;
  constexpr float MAX_LONGITUDE = 180.0f;

  Preferences preferences;
  String bleName;
  bool bleActive = false;
  bool configUpdatePending = false;
  unsigned long bleStopAt = 0;
  String lastConfigError;
  constexpr size_t MAX_SAVED_NETWORKS = 8;
  constexpr size_t MAX_LOG_LINES = 24;
  String debugLog[MAX_LOG_LINES];
  size_t debugLogCount = 0;
  size_t debugLogNext = 0;
  bool testWifiCredentials(const String& ssid, const String& password);

  String savedNetworksJson() {
    // getString su una chiave assente emette un errore NVS nel monitor seriale.
    return preferences.isKey("networks") ? preferences.getString("networks") : "[]";
  }

  void addLegacyNetwork(JsonArray list) {
    if (!preferences.isKey("ssid")) return;
    String legacySsid = preferences.getString("ssid");
    if (legacySsid.isEmpty()) return;
    for (JsonObject network : list) {
      if (network["ssid"].as<String>() == legacySsid) return;
    }
    JsonObject legacy = list.createNestedObject();
    legacy["ssid"] = legacySsid;
    legacy["pass"] = preferences.isKey("pass") ? preferences.getString("pass") : "";
  }

  void rememberNetwork(const String& ssid, const String& password) {
    DynamicJsonDocument networks(2048);
    DeserializationError error = deserializeJson(networks, savedNetworksJson());
    if (error || !networks.is<JsonArray>()) networks.to<JsonArray>();
    JsonArray list = networks.as<JsonArray>();
    if (list.isNull()) list = networks.to<JsonArray>();
    // Migra la rete salvata dalla versione precedente prima di aggiungerne una nuova.
    addLegacyNetwork(list);
    for (JsonObject network : list) {
      if (network["ssid"].as<String>() == ssid) {
        network["pass"] = password;
        String stored; serializeJson(list, stored); preferences.putString("networks", stored);
        return;
      }
    }
    if (list.size() >= MAX_SAVED_NETWORKS) list.remove(0);
    JsonObject network = list.createNestedObject();
    network["ssid"] = ssid;
    network["pass"] = password;
    String stored; serializeJson(list, stored); preferences.putString("networks", stored);
  }

  bool connectSavedWifi() {
    DynamicJsonDocument networks(2048);
    deserializeJson(networks, savedNetworksJson());
    JsonArray list = networks.as<JsonArray>();
    if (list.isNull()) list = networks.to<JsonArray>();
    addLegacyNetwork(list);
    // Ogni giro prova tutte le reti: Wi-Fi 1, Wi-Fi 2, ... e solo dopo
    // riparte dal primo SSID. Evita di attendere tre timeout sulla stessa rete.
    for (uint8_t attempt = 1; attempt <= 3; attempt++) {
      for (JsonObject network : list) {
        String ssid = network["ssid"] | "";
        if (ssid.isEmpty()) continue;
        String password = network["pass"] | "";
        networkManagerRecordLog(String("[WIFI] Tentativo ") + String(attempt) + "/3: " + ssid);
        if (testWifiCredentials(ssid, password)) return true;
      }
    }
    return false;
  }

  bool isValidPin(const String& pin) {
    if (pin.length() != 4) return false;
    for (size_t index = 0; index < pin.length(); index++) {
      if (!isDigit(pin[index])) return false;
    }
    return true;
  }

  bool verifyPin(const String& pin) {
    return isValidPin(pin) && pin == preferences.getString("pin", DEFAULT_PIN);
  }

  void initializeDefaults() {
    if (!preferences.isKey("pin")) preferences.putString("pin", DEFAULT_PIN);
    if (!preferences.isKey("lat")) preferences.putFloat("lat", DEFAULT_LATITUDE);
    if (!preferences.isKey("lon")) preferences.putFloat("lon", DEFAULT_LONGITUDE);
  }

  bool hasWifiCredentials() {
    if (preferences.isKey("ssid") && preferences.getString("ssid").length() > 0) return true;
    DynamicJsonDocument networks(2048);
    if (deserializeJson(networks, savedNetworksJson())) return false;
    JsonArray list = networks.as<JsonArray>();
    return !list.isNull() && list.size() > 0;
  }

  bool testWifiCredentials(const String& ssid, const String& password) {
    networkManagerRecordLog(String("[WIFI] Verifica rete: ") + ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);
    WiFi.begin(ssid.c_str(), password.c_str());

    uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < CONNECT_TIMEOUT_MS) {
      delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
      networkManagerRecordLog("[WIFI] Credenziali rifiutate o rete non raggiungibile");
      WiFi.disconnect(false);
      return false;
    }

    networkManagerRecordLog(String("[WIFI] Rete verificata, IP: ") + WiFi.localIP().toString());
    return true;
  }
}

void networkManagerBegin() {
  uint64_t mac = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", static_cast<uint16_t>(mac & 0xFFFF));
  bleName = String("Robottino-") + suffix;

  preferences.begin("robottino", false);
  initializeDefaults();

  WiFi.mode(WIFI_OFF);
  if (!hasWifiCredentials()) {
    networkManagerStartBle();
  }
  networkManagerRecordLog(String("[BLE] Identita: ") + bleName);
}

void networkManagerLoop() {
  if (bleStopAt != 0 && millis() >= bleStopAt) {
    bleStopAt = 0;
    networkManagerStopBle();
  }
}

void networkManagerStartBle() {
  if (bleActive) return;
  WiFi.disconnect(false);
  bleConfigBegin(bleName);
  bleActive = true;
}

void networkManagerStopBle() {
  if (!bleActive) return;
  bleConfigStop();
  bleActive = false;
}

void networkManagerStopWifi() {
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  Serial.println("[NET] Wi-Fi spento");
}

bool networkManagerConnectForSync() {
  if (!hasWifiCredentials()) {
    networkManagerRecordLog("[WIFI] Nessuna rete salvata");
    return false;
  }

  networkManagerStopBle();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  if (!connectSavedWifi()) {
    networkManagerRecordLog("[WIFI] Connessione a tutte le reti salvate fallita");
    networkManagerStartBle();
    return false;
  }
  networkManagerRecordLog(String("[WIFI] Connesso: ") + WiFi.localIP().toString());
  return true;
}

bool networkManagerIsBleActive() {
  return bleActive;
}

String networkManagerDeviceName() {
  return bleName;
}

bool networkManagerTakeConfigUpdate() {
  bool pending = configUpdatePending;
  configUpdatePending = false;
  return pending;
}

bool networkManagerApplyBlePayload(const String& payload) {
  lastConfigError = "";
  StaticJsonDocument<768> document;
  DeserializationError error = deserializeJson(document, payload);
  if (error) {
    lastConfigError = "JSON non valido";
    networkManagerRecordLog(String("[BLE] Errore JSON: ") + error.c_str());
    return false;
  }

  String action = document["action"] | "";
  if (action == "auth") {
    bool accepted = verifyPin(document["pin"] | "");
    Serial.printf("[BLE] Verifica PIN: %s\n", accepted ? "OK" : "rifiutata");
    return accepted;
  }

  if (action == "scanWifi") {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    int count = WiFi.scanNetworks(false, true);
    DynamicJsonDocument result(4096);
    result["ok"] = true;
    JsonArray networks = result.createNestedArray("networks");
    for (int index = 0; index < count && index < 10; index++) {
      JsonObject network = networks.createNestedObject();
      network["ssid"] = WiFi.SSID(index);
      network["rssi"] = WiFi.RSSI(index);
      network["secure"] = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    // Non disinizializzare il driver qui: BLE e Wi-Fi condividono risorse radio.
    WiFi.disconnect(false);
    String response; serializeJson(result, response);
    bleConfigNotify(response);
    networkManagerRecordLog(String("[WIFI] Scansione completata: ") + String(count) + " reti");
    return true;
  }

  if (action == "savedNetworks") {
    DynamicJsonDocument result(2048);
    result["ok"] = true;
    JsonArray target = result.createNestedArray("networks");
    DynamicJsonDocument stored(2048);
    deserializeJson(stored, savedNetworksJson());
    JsonArray source = stored.as<JsonArray>();
    if (source.isNull()) source = stored.to<JsonArray>();
    addLegacyNetwork(source);
    for (JsonObject network : source) target.add(network["ssid"] | "");
    String response; serializeJson(result, response);
    bleConfigNotify(response);
    return true;
  }

  if (action == "logs") {
    DynamicJsonDocument result(4096);
    result["ok"] = true;
    JsonArray logs = result.createNestedArray("logs");
    size_t linesToSend = debugLogCount < 12 ? debugLogCount : 12;
    size_t first = (debugLogNext + MAX_LOG_LINES - linesToSend) % MAX_LOG_LINES;
    for (size_t index = 0; index < linesToSend; index++) {
      logs.add(debugLog[(first + index) % MAX_LOG_LINES]);
    }
    String response; serializeJson(result, response);
    bleConfigNotify(response);
    return true;
  }

  if (action == "retryWifi") {
    bool connected = connectSavedWifi();
    // Il BLE resta attivo per poter restituire l'esito al telefono.
    if (connected) WiFi.disconnect(false);
    networkManagerRecordLog(connected ? "[WIFI] Riconnessione riuscita" : "[WIFI] Riconnessione fallita");
    bleConfigNotify(connected ? "{\"ok\":true,\"message\":\"Wi-Fi riconnesso\"}" : "{\"ok\":false,\"error\":\"Nessuna rete salvata raggiungibile\"}");
    return connected;
  }

  if (action == "saveWifi") {
    String ssid = document["ssid"] | "";
    String password = document["pass"] | "";
    if (!verifyPin(document["pin"] | "") || ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) {
      bleConfigNotify("{\"ok\":false,\"error\":\"PIN o dati Wi-Fi non validi\"}");
      return false;
    }
    if (!testWifiCredentials(ssid, password)) {
      bleConfigNotify("{\"ok\":false,\"error\":\"SSID o password Wi-Fi errati\"}");
      return false;
    }
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    rememberNetwork(ssid, password);
    networkManagerRecordLog(String("[WIFI] Rete salvata: ") + ssid);
    bleConfigNotify("{\"ok\":true,\"message\":\"Rete Wi-Fi salvata\"}");
    return true;
  }

  if (action == "deleteWifi") {
    String ssidToDelete = document["ssid"] | "";
    if (!verifyPin(document["pin"] | "") || ssidToDelete.isEmpty()) {
      bleConfigNotify("{\"ok\":false,\"error\":\"PIN o rete non validi\"}");
      return false;
    }
    DynamicJsonDocument networks(2048);
    deserializeJson(networks, savedNetworksJson());
    JsonArray list = networks.as<JsonArray>();
    if (list.isNull()) list = networks.to<JsonArray>();
    addLegacyNetwork(list);
    bool removed = false;
    for (size_t index = 0; index < list.size(); index++) {
      if (list[index]["ssid"].as<String>() == ssidToDelete) {
        list.remove(index);
        removed = true;
        break;
      }
    }
    if (removed) {
      String stored; serializeJson(list, stored); preferences.putString("networks", stored);
      if (preferences.isKey("ssid") && preferences.getString("ssid") == ssidToDelete) {
        if (list.size() > 0) {
          preferences.putString("ssid", list[0]["ssid"] | "");
          preferences.putString("pass", list[0]["pass"] | "");
        } else {
          preferences.remove("ssid");
          preferences.remove("pass");
        }
      }
      networkManagerRecordLog(String("[WIFI] Rete rimossa: ") + ssidToDelete);
    }
    bleConfigNotify(removed ? "{\"ok\":true,\"message\":\"Rete eliminata\"}" : "{\"ok\":false,\"error\":\"Rete non trovata\"}");
    return removed;
  }

  if (action == "stopBle") {
    bleConfigNotify("{\"ok\":true,\"message\":\"BLE disattivato\"}");
    bleStopAt = millis() + 400;
    networkManagerRecordLog("[BLE] Disattivazione richiesta dall'app");
    return true;
  }

  String pin = document["pin"] | "";
  String ssid = document["ssid"] | "";
  String password = document["pass"] | "";
  String newPin = document["newPin"] | "";
  unsigned long long epoch = document["time"] | 0ULL;
  float latitude = document["lat"] | NAN;
  float longitude = document["lon"] | NAN;

  bool firstBoot = !preferences.isKey("configured");
  bool pinAccepted = firstBoot ? pin == DEFAULT_PIN : verifyPin(pin);
  if (!pinAccepted || (firstBoot && !isValidPin(newPin)) || ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
      latitude < MIN_LATITUDE || latitude > MAX_LATITUDE ||
      longitude < MIN_LONGITUDE || longitude > MAX_LONGITUDE || epoch < 1000000000ULL) {
    lastConfigError = "PIN o dati non validi";
    networkManagerRecordLog("[BLE] Configurazione rifiutata: dati non validi");
    return false;
  }

  // Verifica la rete prima di scrivere in NVS o spegnere il BLE.
  if (!testWifiCredentials(ssid, password)) {
    lastConfigError = "SSID o password Wi-Fi errati";
    return false;
  }

  time_t seconds = static_cast<time_t>(epoch);
  struct timeval timeValue = {seconds, 0};
  settimeofday(&timeValue, nullptr);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  rememberNetwork(ssid, password);
  preferences.putFloat("lat", latitude);
  preferences.putFloat("lon", longitude);
  if (firstBoot) preferences.putString("pin", newPin);
  preferences.putBool("configured", true);
  networkManagerRecordLog(String("[BLE] Configurazione salvata per ") + ssid);

  configUpdatePending = true;
  return true;
}

String networkManagerLastConfigError() {
  return lastConfigError;
}

String networkManagerProvisioningStatus() {
  StaticJsonDocument<256> document;
  document["firstBoot"] = !preferences.isKey("configured");
  String status;
  serializeJson(document, status);
  return status;
}

void networkManagerRecordLog(const String& message) {
  Serial.println(message);
  debugLog[debugLogNext] = message;
  debugLogNext = (debugLogNext + 1) % MAX_LOG_LINES;
  if (debugLogCount < MAX_LOG_LINES) debugLogCount++;
}

bool networkManagerGetLocation(float& latitude, float& longitude) {
  latitude = preferences.getFloat("lat", DEFAULT_LATITUDE);
  longitude = preferences.getFloat("lon", DEFAULT_LONGITUDE);
  return preferences.isKey("lat") && preferences.isKey("lon");
}
