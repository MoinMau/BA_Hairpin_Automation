#ifndef PARAMS_H
#define PARAMS_H

#include <Arduino.h>

// ============================================================================
// Programm-Parameter
// ----------------------------------------------------------------------------
// Alle Zeiten, Wege, Geschwindigkeiten und Servo-Stellwerte der drei Programme.
// Frueher standen diese Werte fest in updateSequence(); jede Aenderung
// erforderte neu zu flashen. Jetzt sind sie ueber das Menue einstellbar und
// werden im NVS des ESP32 gespeichert, ueberstehen also Neustart und
// Stromausfall.
//
// Alle Felder sind bewusst int32_t: dadurch kann das Menue jedes Feld ueber
// einen einheitlichen Zeiger bearbeiten, ohne pro Typ eine Sonderbehandlung.
// ============================================================================

struct Params {
  int32_t magic;            // Erkennung gueltiger Daten im NVS
  int32_t version;          // Struktur-Version

  // --- Servo-Grundstellung zu Beginn ---
  int32_t servoInit0;
  int32_t servoInit1;
  int32_t servoInit2;
  int32_t servoInit3;
  int32_t servoInit4;
  int32_t servoSettleMs;    // Wartezeit, bis Servos ihre Position erreicht haben

  // --- Achse Y auf Startposition (nur Programm 2 und 3) ---
  int32_t yStartPos;
  int32_t yStartSpeed;

  // --- Achse Z zustellen ---
  int32_t zMovePos;
  int32_t zMoveSpeed;

  // --- Vibration zur Vereinzelung ---
  int32_t vibAmplitude;
  int32_t vibFreqHz;
  int32_t slideWaitMs;      // Nachrutschen lassen (nur Programm 2 und 3)

  // --- Vereinzelung ---
  int32_t feed1Servo1;
  int32_t feed2Servo0;
  int32_t feedDurationMs;

  // --- Greifer schliessen ---
  int32_t gripServo2;
  int32_t gripServo3;
  int32_t gripServo4;

  // --- Uebergabe an den Roboter ---
  int32_t yFeedSteps;
  int32_t yFeedSpeed;
  int32_t robotWaitMs;      // Wartezeit fuer den Roboter (Programm 2 und 3)

  // --- Greifer oeffnen (nur Programm 3) ---
  int32_t openServo2;
  int32_t openServo3;
  int32_t openServo4;
  int32_t openWaitMs;
};

// Parametersaetze der drei Programme, Index 0..2 entspricht Programm 1..3.
extern Params gParams[3];

// Laedt die Werte aus dem NVS. Fehlen sie oder passt die Version nicht,
// werden die Werkseinstellungen gesetzt. Einmal in setup() aufrufen.
void params_begin();

// Markiert die Werte als geaendert. Loest die verzoegerte Speicherung aus.
void params_markDirty();

// True, solange Aenderungen noch nicht im NVS stehen.
bool params_isDirty();

// Schreibt alle drei Saetze sofort ins NVS.
void params_save();

// Setzt einen Satz (0..2) auf die Werkseinstellungen zurueck.
void params_reset(uint8_t idx);

// Muss zyklisch in loop() aufgerufen werden. Speichert verzoegert, damit beim
// Durchtippen eines Werts nicht bei jedem Schritt ins Flash geschrieben wird.
void params_tick();

#endif // PARAMS_H
