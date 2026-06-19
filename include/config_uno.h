#ifndef CONFIG_UNO_H
#define CONFIG_UNO_H

#include <Arduino.h>

// ============================================================
// 1. PIN-BELEGUNG (CNC Shield V3)
// ============================================================

#define STEPPER_X_STP 2   // X Step
#define STEPPER_X_DIR 5   // X Direction
#define STEPPER_Y_STP 3   // Y Step
#define STEPPER_Y_DIR 6   // Y Direction
#define STEPPER_Z_STP 4   // Z Step
#define STEPPER_Z_DIR 7   // Z Direction
#define STEPPER_EN    8   // Enable (LOW = Treiber an)

// Endschalter-Pins (X=Pin9, Y=Pin10, Z=Pin11)
static const uint8_t ENDSTOP_PINS[3] = {9, 10, 11};

// ============================================================
// 2. RICHTUNGSUMKER
//    true  = DIR-Pin wird invertiert (Achse fährt entgegengesetzt)
//    false = Normalbetrieb
//    Nützlich, wenn die Verdrahtung oder Mechanik die
//    Richtung umkehrt.
// ============================================================

#define INVERT_X_DIR  false
#define INVERT_Y_DIR  false
#define INVERT_Z_DIR  false

// Array für indexbasierten Zugriff
static const bool INVERT_DIR[3] = {INVERT_X_DIR, INVERT_Y_DIR, INVERT_Z_DIR};

// ============================================================
// 3. STANDARD-GESCHWINDIGKEITEN (Steps/s)
//    Wird verwendet, wenn kein Speed-Parameter übergeben wird
// ============================================================

#define X_DEFAULT_SPEED  1000.0
#define Y_DEFAULT_SPEED  800.0
#define Z_DEFAULT_SPEED  600.0

// ============================================================
// 4. SOFT-LIMITS (maximale Verfahrwege in Steps)
// ============================================================

// --- Achse X ---
const long  X_MIN_POS    = 0;
const long  X_MAX_POS    = 8000;
const float X_MAX_SPEED  = 2000.0;

// --- Achse Y ---
const long  Y_MIN_POS    = 0;
const long  Y_MAX_POS    = 5000;
const float Y_MAX_SPEED  = 1500.0;

// --- Achse Z ---
const long  Z_MIN_POS    = 0;
const long  Z_MAX_POS    = 3000;
const float Z_MAX_SPEED  = 1000.0;

// Hilfs-Arrays (indexbasiert: 0=X, 1=Y, 2=Z)
static const long   MIN_POS[3]          = {X_MIN_POS, Y_MIN_POS, Z_MIN_POS};
static const long   MAX_POS[3]          = {X_MAX_POS, Y_MAX_POS, Z_MAX_POS};
static const float  MAX_SPEED_LIMITS[3] = {X_MAX_SPEED, Y_MAX_SPEED, Z_MAX_SPEED};

#define MAX_STEPS_X  X_MAX_POS  // Legacy Support

// ============================================================
// 5. HOMING-KONFIGURATION (pro Achse einstellbar)
// ============================================================

// Geschwindigkeit während der Endschalter-Suche (Steps/s)
// NEGATIV = Rückwärts zum Endschalter fahren
#define HOMING_SEARCH_SPEED  -100.0

// Geschwindigkeit beim Zurückfahren vom Endschalter (Steps/s)
#define HOMING_REBOUND_SPEED  50.0

// Strecke, die nach Endschalter-Kontakt zurückgefahren wird (Steps)
#define HOMING_REBOUND_STEPS  20

// ============================================================
// 6. DEFAULT-BESCHLEUNIGUNG
// ============================================================

#define DEFAULT_MAX_SPEED  1000.0
#define DEFAULT_ACCEL      500.0

#endif