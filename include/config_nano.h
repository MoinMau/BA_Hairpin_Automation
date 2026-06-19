#ifndef CONFIG_NANO_H
#define CONFIG_NANO_H

#include <Arduino.h>

// --- Hardware Pin Belegungen ---
static const uint8_t SERVO_PINS[6] = {7, 8, 9, 10, 11, 12}; 
#define PIN_SHUNT       A0                                 
#define PIN_MH_DIGITAL  2                             
#define PIN_MH_ANALOG   A7                             

// --- Standardwerte ---
#define DEFAULT_SERVO_VAL 150 

// --- Individuelle Servo Limits (0 - 1000 Bereich) ---
const uint16_t S0_MIN = 100; const uint16_t S0_MAX = 490;
const uint16_t S1_MIN = 100; const uint16_t S1_MAX = 440;
const uint16_t S2_MIN = 100; const uint16_t S2_MAX = 900;
const uint16_t S3_MIN = 100; const uint16_t S3_MAX = 900;
const uint16_t S4_MIN = 100; const uint16_t S4_MAX = 900;
const uint16_t S5_MIN = 100; const uint16_t S5_MAX = 900;

// Hilfs-Arrays für die Logik
static const uint16_t SERVO_MIN_LIMITS[6] = {
  S0_MIN, S1_MIN, S2_MIN, S3_MIN, S4_MIN, S5_MIN
};

static const uint16_t SERVO_MAX_LIMITS[6] = {
  S0_MAX, S1_MAX, S2_MAX, S3_MAX, S4_MAX, S5_MAX
};

// ============================================================
// SERVO-RICHTUNGSUMKER
//   false (Standard): 0 = 500us (min/zurückgezogen),
//                      1000 = 2500us (max/ausgefahren)
//   true:             0 = 2500us (max/ausgefahren),
//                      1000 = 500us (min/zurückgezogen)
//   Nützlich, wenn die Mechanik umgekehrt arbeitet.
// ============================================================

const bool S0_INVERT = false;
const bool S1_INVERT = false;
const bool S2_INVERT = false;
const bool S3_INVERT = false;
const bool S4_INVERT = false;
const bool S5_INVERT = false;

static const bool SERVO_INVERT[6] = {
  S0_INVERT, S1_INVERT, S2_INVERT, S3_INVERT, S4_INVERT, S5_INVERT
};

#endif