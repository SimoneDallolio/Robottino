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

enum StartupState {
  STARTUP_CONNECTING,
  STARTUP_LOADING,
  STARTUP_CONFIGURE,
  STARTUP_READY
};

StartupState startupState = STARTUP_CONNECTING;

void renderStartupScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (startupState == STARTUP_CONNECTING) {
    display.setCursor(18, 24);
    display.print("Connessione WiFi...");
  } else if (startupState == STARTUP_LOADING) {
    display.setCursor(25, 20);
    display.print("Avvio in corso");
    display.setCursor(35, 38);
    display.print("Attendere...");
  } else if (startupState == STARTUP_CONFIGURE) {
    display.setCursor(8, 12);
    display.print(networkManagerDeviceName());
    display.setCursor(7, 29);
    display.print("Configura il robot");
    display.setCursor(13, 43);
    display.print("dall'app BLE");
  }

  display.display();
}

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

  // Mostra lo stato di avvio prima di iniziare il collegamento.
  renderStartupScreen();

  // Carica identita, reti NVS e portale di configurazione.
  networkManagerBegin();
  if (networkManagerIsBleActive()) {
    startupState = STARTUP_CONFIGURE;
    renderStartupScreen();
  } else {
    startupState = STARTUP_LOADING;
    renderStartupScreen();
    // Sincronizza ora e temperatura, poi spegne il Wi-Fi.
    if (initTimeNTP()) {
      startupState = STARTUP_READY;
    } else {
      startupState = STARTUP_CONFIGURE;
    }
  }

  display.clearDisplay();
  display.display();
  lastActivityTime = millis();
}

void loop() {
  networkManagerLoop();

  if (isButtonHeldFor(3000)) {
    networkManagerStartBle();
    networkManagerRecordLog("[INPUT] Pressione prolungata: BLE riattivato");
    lastActivityTime = millis();
  }

  if (networkManagerTakeConfigUpdate()) {
    networkManagerStopBle();
    startupState = STARTUP_LOADING;
    renderStartupScreen();
    if (initTimeNTP()) {
      startupState = STARTUP_READY;
    } else {
      startupState = STARTUP_CONFIGURE;
    }
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
  if (startupState == STARTUP_READY) {
    updateDisplay();
  } else {
    renderStartupScreen();
  }
  delay(20);
}
