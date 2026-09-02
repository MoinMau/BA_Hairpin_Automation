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

  delay(20);
  buttons_selfTest();
}

// ----------------------------------------------------------------------------
// Selbsttest der Eingaenge
// ----------------------------------------------------------------------------
// Jeder Pin wird einmal mit internem Pullup und einmal mit internem Pulldown
// gelesen. Aus der Kombination laesst sich der Fehler eindeutig zuordnen:
//
//   Pullup HIGH / Pulldown LOW   -> Pin haengt an nichts ausser dem Taster.
//                                   Genau so muss es im Ruhezustand sein.
//   Pullup LOW  / Pulldown LOW   -> Pin ist fest mit GND verbunden. Entweder
//                                   Kurzschluss auf der Platine oder der
//                                   Taster ist dauerhaft geschlossen (bei
//                                   4-poligen Mikrotastern: die beiden intern
//                                   verbundenen Beine erwischt).
//   Pullup HIGH / Pulldown HIGH  -> Pin ist fest mit 3V3 verbunden.
//
// Ein Pin, der dem jeweiligen Pull folgt, ist offen; ein Pin, der auf einem
// Pegel klebt, ist extern festgehalten. Damit ist geklaert, ob der Fehler in
// der Firmware oder in der Verdrahtung liegt.
void buttons_selfTest() {
  Serial.println(F("\n[BTN] Selbsttest der Eingaenge:"));

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    uint8_t pin = BTN_PINS[i];

    pinMode(pin, INPUT_PULLUP);
    delay(2);
    bool up = (digitalRead(pin) == HIGH);

    pinMode(pin, INPUT_PULLDOWN);
    delay(2);
    bool dn = (digitalRead(pin) == HIGH);

    pinMode(pin, INPUT_PULLUP);   // Betriebszustand wiederherstellen
    delay(2);

    Serial.print(F("  "));
    Serial.print(BTN_NAMES[i]);
    Serial.print(F("\tGPIO"));
    Serial.print(pin);
    Serial.print(F("\t"));

    if (up && !dn) {
      Serial.println(F("offen - ok"));
    } else if (!up && !dn) {
      Serial.println(F("FEST AUF GND - Kurzschluss oder Taster dauerhaft zu"));
    } else if (up && dn) {
      Serial.println(F("FEST AUF 3V3 - falsch verdrahtet"));
    } else {
      Serial.println(F("unplausibel - Verdrahtung pruefen"));
    }

    // Alles, was jetzt LOW ist, wird gesperrt, bis es einmal HIGH war.
    if (digitalRead(pin) == LOW) {
      btnLocked[i]  = true;
      btnLastRaw[i] = true;
    }
  }
  Serial.println();
}

// Solange ein Eingang klemmt, alle zwei Sekunden die Rohpegel ausgeben.
// Damit laesst sich beim Messen direkt verfolgen, welcher Draht haengt.
// Sobald alles frei ist, verstummt die Ausgabe von selbst.
static void reportRawWhileLocked() {
  static unsigned long last = 0;
  bool any = false;
  for (uint8_t i = 0; i < BTN_COUNT; i++) if (btnLocked[i]) { any = true; break; }
  if (!any) return;
  if (millis() - last < 2000) return;
  last = millis();

  Serial.print(F("[BTN] roh:"));
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    Serial.print(' ');
    Serial.print(BTN_NAMES[i]);
    Serial.print('=');
    Serial.print(digitalRead(BTN_PINS[i]) == LOW ? F("LOW") : F("HIGH"));
  }
  Serial.println();
}

ButtonId buttons_update() {
  unsigned long now = millis();
  ButtonId event = BTN_NONE;

  reportRawWhileLocked();

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
