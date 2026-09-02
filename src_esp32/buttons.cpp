#include "buttons.h"
#include "config_display.h"


// Zuordnung ButtonId -> GPIO (Reihenfolge muss zum enum passen)
static const uint8_t BTN_PINS[BTN_COUNT] = {
  BTN_PIN_UP, BTN_PIN_DOWN, BTN_PIN_LEFT, BTN_PIN_RIGHT, BTN_PIN_ENTER
};

static const char* BTN_NAMES[BTN_COUNT] = {
  "UP", "DOWN", "LEFT", "RIGHT", "ENTER"
};

// Eingangsbeschaltung, siehe config_display.h. Der Rest des Moduls arbeitet
// nur noch mit "gedrueckt ja/nein" und kennt die Polaritaet nicht.
#if BTN_ACTIVE_HIGH
  #define BTN_INPUT_MODE   INPUT_PULLDOWN
  #define BTN_IS_PRESSED(p) (digitalRead(p) == HIGH)
#else
  #define BTN_INPUT_MODE   INPUT_PULLUP
  #define BTN_IS_PRESSED(p) (digitalRead(p) == LOW)
#endif

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
    pinMode(BTN_PINS[i], BTN_INPUT_MODE);
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
// Prueft, ob ein Eingang schon im Ruhezustand als gedrueckt gelesen wird. Das
// ist praktisch immer ein Verdrahtungsfehler. Sieht ein Pin falsch aus, wird er
// zusaetzlich gegen internen Pullup und Pulldown gelesen: ein offener Pin folgt
// dem jeweiligen Pull, ein extern festgehaltener klebt auf seinem Pegel. Damit
// ist entschieden, ob der Pin an GND oder an 3V3 haengt.
void buttons_selfTest() {
  Serial.println(F("\n[BTN] Selbsttest der Eingaenge:"));

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    uint8_t pin = BTN_PINS[i];

    pinMode(pin, BTN_INPUT_MODE);
    delay(2);
    bool pressed = BTN_IS_PRESSED(pin);

    Serial.print(F("  "));
    Serial.print(BTN_NAMES[i]);
    Serial.print(F("\tGPIO"));
    Serial.print(pin);
    Serial.print(F("\t"));

    if (!pressed) {
      Serial.println(F("Ruhezustand - ok"));
      continue;
    }

    // Sieht falsch aus -> genauer nachsehen, woran der Pin haengt
    pinMode(pin, INPUT_PULLUP);   delay(2);
    bool up = (digitalRead(pin) == HIGH);
    pinMode(pin, INPUT_PULLDOWN); delay(2);
    bool dn = (digitalRead(pin) == HIGH);
    pinMode(pin, BTN_INPUT_MODE); delay(2);

    if (up && dn)       Serial.println(F("liegt fest auf 3V3 - Taster dauerhaft zu?"));
    else if (!up && !dn) Serial.println(F("liegt fest auf GND"));
    else                 Serial.println(F("wird als GEDRUECKT gelesen - Verdrahtung pruefen"));

    btnLocked[i]  = true;
    btnLastRaw[i] = true;   // damit die Freigabe eine echte Flanke sieht
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
    Serial.print(BTN_IS_PRESSED(BTN_PINS[i]) ? F("gedrueckt") : F("frei"));
  }
  Serial.println();
}

ButtonId buttons_update() {
  unsigned long now = millis();
  ButtonId event = BTN_NONE;

  reportRawWhileLocked();

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    bool raw = BTN_IS_PRESSED(BTN_PINS[i]);

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
