#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

// ============================================================================
// Tasten-Modul: entprellte 5-Wege-Navigation, komplett nicht-blockierend.
// Alle Zustaende werden ueber millis() verwaltet, es wird nie delay() genutzt.
// ============================================================================

enum ButtonId {
  BTN_UP = 0,
  BTN_DOWN,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_ENTER,
  BTN_COUNT,
  BTN_NONE = 255
};

// Initialisiert die GPIOs als INPUT_PULLUP und fuehrt den Selbsttest aus.
void buttons_begin();

// Prueft jeden Eingang gegen internen Pullup und Pulldown und meldet auf
// Serial, ob er offen ist oder extern auf GND bzw. 3V3 festgehalten wird.
void buttons_selfTest();

// Muss zyklisch in loop() aufgerufen werden. Liefert genau ein Ereignis
// pro Aufruf zurueck (BTN_NONE wenn nichts passiert ist).
// Enthaelt Entprellung und Autorepeat fuer UP/DOWN/LEFT/RIGHT.
ButtonId buttons_update();

// Roher, entprellter Zustand einer Taste (true = gerade gedrueckt).
// Wird fuer den Tasten-Testscreen gebraucht.
bool buttons_isDown(ButtonId id);

// True, wenn die Taste beim Start bereits gedrueckt war und deshalb gesperrt
// ist (fast immer ein Verdrahtungsfehler). Die Sperre faellt, sobald die Taste
// einmal losgelassen wurde.
bool buttons_isLocked(ButtonId id);

// Klartextname fuer Debug-Ausgaben und Anzeige.
const char* buttons_name(ButtonId id);

#endif // BUTTONS_H
