#include "NetworkManager.h"
#include "BLEConfigModule.h"
#include "Config.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

namespace {
  constexpr uint32_t CONNECT_TIMEOUT_MS = 12000;
  constexpr char DEFAULT_PIN[] = "1234";
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
    return preferences.isKey("ssid") && preferences.getString("ssid").length() > 0;
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
  Serial.printf("[NET] Identita BLE: %s\n", bleName.c_str());
}

void networkManagerLoop() {
  bleConfigLoop();
}

void networkManagerStartBle() {
  if (bleActive) return;
  WiFi.mode(WIFI_OFF);
  bleConfigBegin(bleName);
  bleActive = true;
}

void networkManagerStopBle() {
  if (!bleActive) return;
  bleConfigStop();
  bleActive = false;
}

void networkManagerStopWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[NET] Wi-Fi spento");
}

bool networkManagerConnectForSync() {
  if (!hasWifiCredentials()) {
    Serial.println("[NET] Nessuna credenziale Wi-Fi in NVS");
    return false;
  }

  networkManagerStopBle();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("pass", "");
  WiFi.begin(ssid.c_str(), password.c_str());

  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < CONNECT_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NET] Connessione Wi-Fi fallita");
    networkManagerStartBle();
    return false;
  }
  Serial.printf("[NET] Wi-Fi connesso: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

bool networkManagerIsBleActive() {
  return bleActive;
}

bool networkManagerTakeConfigUpdate() {
  bool pending = configUpdatePending;
  configUpdatePending = false;
  return pending;
}

bool networkManagerApplyBlePayload(const String& payload) {
  StaticJsonDocument<768> document;
  DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.printf("[BLE] JSON non valido: %s\n", error.c_str());
    return false;
  }

  String pin = document["pin"] | "";
  String ssid = document["ssid"] | "";
  String password = document["pass"] | "";
  unsigned long long epoch = document["time"] | 0ULL;
  float latitude = document["lat"] | NAN;
  float longitude = document["lon"] | NAN;

  if (!verifyPin(pin) || ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
      latitude < MIN_LATITUDE || latitude > MAX_LATITUDE ||
      longitude < MIN_LONGITUDE || longitude > MAX_LONGITUDE || epoch < 1000000000ULL) {
    Serial.println("[BLE] Configurazione rifiutata: PIN o dati non validi");
    return false;
  }

  time_t seconds = static_cast<time_t>(epoch);
  struct timeval timeValue = {seconds, 0};
  settimeofday(&timeValue, nullptr);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  preferences.putFloat("lat", latitude);
  preferences.putFloat("lon", longitude);
  Serial.printf("[BLE] Configurazione salvata per %s\n", ssid.c_str());

  configUpdatePending = true;
  return true;
}

bool networkManagerGetLocation(float& latitude, float& longitude) {
  latitude = preferences.getFloat("lat", DEFAULT_LATITUDE);
  longitude = preferences.getFloat("lon", DEFAULT_LONGITUDE);
  return preferences.isKey("lat") && preferences.isKey("lon");
}
