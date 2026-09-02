#ifndef MACHINE_API_H
#define MACHINE_API_H

#include <Arduino.h>
#include "i2c_protocol.h"

// ============================================================================
// Schnittstelle zur Maschinensteuerung
// ----------------------------------------------------------------------------
// Diese Funktionen sind in main.cpp implementiert. Der Header macht sie fuer
// die Menuefuehrung nutzbar, ohne dass Code verschoben werden muss - die
// erprobte Ablaufsteuerung bleibt damit unveraendert.
//
// Menue und serielle Konsole rufen dieselben Funktionen auf. Keine der beiden
// Bedienarten muss die andere kennen.
// ============================================================================

// --- Schrittmotoren (Uno 0x33) ---
void axis_abs(uint8_t axis, int32_t position, int16_t speed);
void axis_rel(uint8_t axis, int32_t steps, int16_t speed);
void axis_vibrate(uint8_t axis, int32_t amplitude, int16_t freq_hz);
void axis_stop(uint8_t axis);
void axis_home(uint8_t axis);
void axis_enable();
void axis_disable();
StepperStatus get_stepper_status();

// --- Servos (Nano 0x32) ---
void servo_set(uint8_t num, uint16_t value);   // Wert 0-1000
bool servo_readAll(uint16_t out[6]);           // Ist-Stellwerte, false = keine Antwort

// --- Ablaufsteuerung ---
void startHairpinSequence(int program, int runs);
bool sequence_isRunning();
int  sequence_program();         // 1..3, nur gueltig waehrend eines Laufs
int  sequence_remainingRuns();
void sequence_abort();           // bricht ab und stoppt alle Achsen

#endif // MACHINE_API_H
