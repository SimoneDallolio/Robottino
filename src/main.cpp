#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "Config.h"
#include "InputHandler.h"
#include "FaceModule.h"
#include "ClockModule.h"
#include "NetworkManager.h"

// Modulo principale: inizializza l'hardware, sceglie la schermata da mostrare
// e gestisce le interazioni con il pulsante.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Stato iniziale del robot: volto sveglio.
ScreenMode currentScreen = MODE_FACE;
FaceState faceState = FACE_AWAKE;

// Timestamp usati per calcolare inattivita e durata dell'addormentamento.
unsigned long lastActivityTime = 0;
unsigned long sleepAnimationStart = 0;
unsigned long wakeAnimationStart = 0;

void updateDisplay() {
  // Ogni fotogramma viene preparato in memoria e poi trasferito all'OLED in
  // un'unica operazione, evitando residui del fotogramma precedente.
  display.clearDisplay();

  if (currentScreen == MODE_FACE) {
    if (faceState == FACE_AWAKE) {
      renderFaceAwake(display, 0);
    } else if (faceState == FACE_FALLING_ASLEEP) {
      renderFaceFallingAsleep(display, sleepAnimationStart, faceState);
    } else if (faceState == FACE_SLEEPING) {
      renderFaceSleeping(display);
    } else if (faceState == FACE_WAKING_UP) {
      renderFaceWakingUp(display, wakeAnimationStart, faceState);
    }
  } 
  else if (currentScreen == MODE_CLOCK) {
    renderClockScreen(display);
  }

  // Mostra il fotogramma appena composto sul display fisico.
  display.display();
}

void setup() {
  // Inizializza seriale, pulsante, bus I2C e controller del display.
  Serial.begin(115200);
  initInput();
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("Errore: SSD1306 non trovato"));
    for (;;);
  }

  // Mostra un messaggio mentre il modulo orologio cerca la rete Wi-Fi.
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 28);
  display.print("Connessione WiFi...");
  display.display();

  // Carica identita, reti NVS e portale di configurazione.
  networkManagerBegin();
  // Sincronizza ora e temperatura, poi spegne il Wi-Fi.
  initTimeNTP();

  display.clearDisplay();
  display.display();
  lastActivityTime = millis();
}

void loop() {
  networkManagerLoop();

  if (isButtonHeldFor(3000)) {
    networkManagerStartBle();
    lastActivityTime = millis();
  }

  if (networkManagerTakeConfigUpdate()) {
    networkManagerStopBle();
    initTimeNTP();
  }

  // Una pressione sveglia il robot oppure alterna tra volto e orologio.
  if (isButtonPressed()) {
    lastActivityTime = millis();

    if (faceState == FACE_SLEEPING || faceState == FACE_FALLING_ASLEEP) {
      faceState = FACE_WAKING_UP;
      wakeAnimationStart = millis();
      currentScreen = MODE_FACE;
    } else if (currentScreen == MODE_FACE) {
      currentScreen = MODE_CLOCK;
    } else if (currentScreen == MODE_CLOCK) {
      currentScreen = MODE_FACE;
    }
  }

  // Dopo il timeout, l'orologio torna al volto; se il volto era gia sveglio,
  // parte invece l'animazione che lo porta allo stato di sonno.
  if (millis() - lastActivityTime > INACTIVITY_TIMEOUT) {
    if (currentScreen == MODE_CLOCK) {
      currentScreen = MODE_FACE;
    } else if (currentScreen == MODE_FACE && faceState == FACE_AWAKE) {
      faceState = FACE_FALLING_ASLEEP;
      sleepAnimationStart = millis();
    }
  }

  // Ridisegna continuamente la schermata per mantenere vive le animazioni.
  clockModuleLoop();
  updateDisplay();
  delay(20);
}