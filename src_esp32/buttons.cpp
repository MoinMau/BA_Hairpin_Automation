#include "buttons.h"
#include "config_display.h"

// Zuordnung ButtonId -> GPIO (Reihenfolge muss zum enum passen)
static const uint8_t BTN_PINS[BTN_COUNT] = {
  BTN_PIN_UP, BTN_PIN_DOWN, BTN_PIN_LEFT, BTN_PIN_RIGHT, BTN_PIN_ENTER
};

static const char* BTN_NAMES[BTN_COUNT] = {
  "UP", "DOWN", "LEFT", "RIGHT", "ENTER"
};

// Zustandsspeicher je Taste
static bool          btnStable[BTN_COUNT];      // entprellter Zustand (true = gedrueckt)
static bool          btnLastRaw[BTN_COUNT];     // letzter Rohwert
static unsigned long btnLastChange[BTN_COUNT];  // Zeitpunkt der letzten Flanke
static unsigned long btnNextRepeat[BTN_COUNT];  // naechster Autorepeat-Zeitpunkt

// Beim Start bereits gedrueckte Tasten werden gesperrt. Das ist fast immer ein
// Verdrahtungsfehler (Draht gegen GND, kalte Loetstelle, vertauschter Pin) und
// nicht der Wille des Bedieners. Ohne diese Sperre wuerde eine einzige
// klemmende Taste die gesamte Bedienung blockieren.
// Die Sperre faellt, sobald die Taste einmal sauber losgelassen wurde.
static bool btnLocked[BTN_COUNT];

// ENTER bekommt bewusst KEIN Autorepeat, damit Aktionen nicht mehrfach
// ausgeloest werden, wenn man die Taste zu lange haelt.
static bool btnRepeatAllowed(ButtonId id) {
  return id != BTN_ENTER;
}

void buttons_begin() {
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btnStable[i]     = false;
    btnLastRaw[i]    = false;
    btnLastChange[i] = 0;
    btnNextRepeat[i] = 0;
    btnLocked[i]     = false;
  }

  // Pullups einschwingen lassen, dann auf klemmende Tasten pruefen.
  delay(20);
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (digitalRead(BTN_PINS[i]) != LOW) continue;
    btnLocked[i]  = true;
    btnLastRaw[i] = true;   // damit die Freigabe eine echte Flanke sieht
    Serial.print(F("[BTN] "));
    Serial.print(BTN_NAMES[i]);
    Serial.print(F(" (GPIO"));
    Serial.print(BTN_PINS[i]);
    Serial.println(F(") liegt beim Start auf LOW -> gesperrt."));
  }
}

ButtonId buttons_update() {
  unsigned long now = millis();
  ButtonId event = BTN_NONE;

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    // Pullup: gedrueckt = LOW
    bool raw = (digitalRead(BTN_PINS[i]) == LOW);

    if (raw != btnLastRaw[i]) {
      btnLastRaw[i]    = raw;
      btnLastChange[i] = now;
      continue;  // Flanke gerade erst gesehen -> erst entprellen
    }

    if ((now - btnLastChange[i]) < BTN_DEBOUNCE_MS) continue;

    // Beim Start klemmende Taste: erst freigeben, wenn sie losgelassen wurde
    if (btnLocked[i]) {
      if (!raw) {
        btnLocked[i] = false;
        Serial.print(F("[BTN] "));
        Serial.print(BTN_NAMES[i]);
        Serial.println(F(" losgelassen -> freigegeben."));
      }
      continue;
    }

    if (raw && !btnStable[i]) {
      // Saubere fallende Flanke -> Tastendruck
      btnStable[i]     = true;
      btnNextRepeat[i] = now + BTN_REPEAT_DELAY_MS;
      if (event == BTN_NONE) event = (ButtonId)i;
    }
    else if (!raw && btnStable[i]) {
      // Taste losgelassen
      btnStable[i] = false;
    }
    else if (raw && btnStable[i] && btnRepeatAllowed((ButtonId)i)) {
      // Taste gehalten -> Autorepeat
      if (now >= btnNextRepeat[i]) {
        btnNextRepeat[i] = now + BTN_REPEAT_RATE_MS;
        if (event == BTN_NONE) event = (ButtonId)i;
      }
    }
  }
  return event;
}

bool buttons_isDown(ButtonId id) {
  if (id >= BTN_COUNT) return false;
  return btnStable[id] && !btnLocked[id];
}

bool buttons_isLocked(ButtonId id) {
  if (id >= BTN_COUNT) return false;
  return btnLocked[id];
}

const char* buttons_name(ButtonId id) {
  if (id >= BTN_COUNT) return "-";
  return BTN_NAMES[id];
}
