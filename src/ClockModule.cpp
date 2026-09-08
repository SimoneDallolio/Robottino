#include "ClockModule.h"
#include "Config.h"
#include "NetworkManager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "time.h"

// Server e regola del fuso orario usati dalla sincronizzazione NTP.
const char* ntpServer = "pool.ntp.org";
// Configurazione fuso orario Italia (CET/CEST) con gestione automatica ora legale
const char* timeZone = "CET-1CEST,M3.5.0,M10.5.0/3"; 

const char* months[] = {"GEN", "FEB", "MAR", "APR", "MAG", "GIU",
                        "LUG", "AGO", "SET", "OTT", "NOV", "DIC"};

float currentTemperature = NAN;
constexpr unsigned long ECO_SYNC_INTERVAL = 30UL * 60UL * 1000UL;
unsigned long lastEcoSync = 0;

static bool getWeatherForCoordinates(float latitude, float longitude) {
  WiFiClientSecure client;
  client.setInsecure();

  String weatherUrl = "https://api.open-meteo.com/v1/forecast?latitude=" +
                      String(latitude, 4) + "&longitude=" + String(longitude, 4) +
                      "&current=temperature_2m&timezone=auto";
  HTTPClient weatherRequest;
  weatherRequest.setConnectTimeout(5000);
  weatherRequest.setTimeout(5000);
  weatherRequest.useHTTP10(true);
  if (!weatherRequest.begin(client, weatherUrl)) {
    Serial.println("[METEO] Errore apertura richiesta Open-Meteo");
    return false;
  }

  int weatherStatus = weatherRequest.GET();
  Serial.printf("[METEO] Open-Meteo HTTP status: %d\n", weatherStatus);
  if (weatherStatus != HTTP_CODE_OK) {
    Serial.printf("[METEO] Risposta meteo: %s\n", weatherRequest.errorToString(weatherStatus).c_str());
    weatherRequest.end();
    return false;
  }

  StaticJsonDocument<512> weatherDocument;
  // Legge il JSON direttamente dallo stream HTTP, evitando una seconda copia
  // completa della risposta nella RAM dell'ESP32.
  DeserializationError weatherError = deserializeJson(weatherDocument, weatherRequest.getStream());
  weatherRequest.end();
  if (weatherError) {
    Serial.printf("[METEO] Errore JSON meteo: %s\n", weatherError.c_str());
    return false;
  }

  currentTemperature = weatherDocument["current"]["temperature_2m"].as<float>();
  if (isnan(currentTemperature)) {
    Serial.println("[METEO] Campo current.temperature_2m assente o non numerico");
    return false;
  }

  Serial.printf("[METEO] Temperatura: %.1f C\n", currentTemperature);
  return true;
}

static bool syncTimeAndWeather() {
  if (!networkManagerConnectForSync()) {
    Serial.println("[NTP] Nessuna rete disponibile per il refresh");
    lastEcoSync = millis();
    return false;
  }

  Serial.printf("[NTP] Wi-Fi connesso, IP locale: %s\n", WiFi.localIP().toString().c_str());
    // Imposta il fuso orario italiano e avvia la sincronizzazione con il server.
    configTzTime(timeZone, ntpServer);
    
    // Attende la prima sincronizzazione dell'ora
    struct tm timeinfo;
    int syncAttempts = 0;
    while (!getLocalTime(&timeinfo) && syncAttempts < 10) {
      delay(500);
      syncAttempts++;
    }

    if (syncAttempts < 10) {
      Serial.println("[NTP] Ora sincronizzata");
    } else {
      Serial.println("[NTP] Sincronizzazione ora fallita");
    }

    float latitude;
    float longitude;
    networkManagerGetLocation(latitude, longitude);
    Serial.printf("[METEO] Coordinate NVS: lat %.4f, lon %.4f\n", latitude, longitude);
    bool weatherUpdated = getWeatherForCoordinates(latitude, longitude);
    if (!weatherUpdated) {
      Serial.println("[METEO] Recupero dati fallito: verra mostrato -- C");
    }

  networkManagerStopWifi();
  lastEcoSync = millis();
  return syncAttempts < 10 && weatherUpdated;
}

bool initTimeNTP() {
  return syncTimeAndWeather();
}

void clockModuleLoop() {
  if (!networkManagerIsBleActive() && millis() - lastEcoSync >= ECO_SYNC_INTERVAL) {
    syncTimeAndWeather();
  }
}

void renderClockScreen(Adafruit_SSD1306& display) {
  // Recupera l'ora locale e la disegna solo se la sincronizzazione e riuscita.
  struct tm timeinfo;
  bool hasTime = getLocalTime(&timeinfo);

  if (hasTime) {
    // Data abbreviata nell'angolo in alto a sinistra.
    char dateText[8];
    snprintf(dateText, sizeof(dateText), "%s %02d", months[timeinfo.tm_mon], timeinfo.tm_mday);
    display.setTextSize(1);
    display.setCursor(2, 2);
    display.print(dateText);

    // L'orario resta centrato nel display.
    char timeText[6];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    display.setTextSize(3);
    int16_t textX;
    int16_t textY;
    uint16_t textWidth;
    uint16_t textHeight;
    display.getTextBounds(timeText, 0, 0, &textX, &textY, &textWidth, &textHeight);
    // Compensa l'origine interna del font per centrare il rettangolo reale
    // del testo, non soltanto il punto di inserimento del cursore.
    display.setCursor((SCREEN_WIDTH - textWidth) / 2 - textX,
              (SCREEN_HEIGHT - textHeight) / 2 - textY);
    display.print(timeText);

    // Mini termometro in basso a destra. La temperatura viene ottenuta da
    // Open-Meteo usando le coordinate salvate in NVS.
    display.setTextSize(1);
    char temperatureValue[8];
    if (!isnan(currentTemperature)) {
      snprintf(temperatureValue, sizeof(temperatureValue), "%.1f", currentTemperature);
    } else {
      snprintf(temperatureValue, sizeof(temperatureValue), "--");
    }
    display.getTextBounds(temperatureValue, 0, 0, &textX, &textY, &textWidth, &textHeight);

    // Il font standard del display non contiene sempre il carattere "°".
    // Lo disegniamo quindi come un piccolo cerchio tra il valore e la C.
    int temperatureY = SCREEN_HEIGHT - textHeight - 1;
    int temperatureX = SCREEN_WIDTH - textWidth - 12;
    display.setCursor(temperatureX, temperatureY);
    display.print(temperatureValue);
    display.drawCircle(temperatureX + textWidth + 3, temperatureY + 2, 1, SSD1306_WHITE);
    display.setCursor(temperatureX + textWidth + 6, temperatureY);
    display.print("C");
  } else {
    display.setTextSize(1);
    display.setCursor(29, 28);
    display.print("Ora non sync");
  }
}