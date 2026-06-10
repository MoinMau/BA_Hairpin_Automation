#ifndef CONFIG_UNO_H
#define CONFIG_UNO_H

#include <Arduino.h>

// --- Hardware Pin Belegungen (CNC Shield) ---
#define STEPPER_X_STP 2   
#define STEPPER_X_DIR 5   
#define STEPPER_Y_STP 3   
#define STEPPER_Y_DIR 6   
#define STEPPER_Z_STP 4   
#define STEPPER_Z_DIR 7   
#define STEPPER_EN    8      

static const uint8_t ENDSTOP_PINS[3] = {9, 10, 11}; // X=Pin 9, Y=Pin 10, Z=Pin 11

// --- Standardwerte & Limits ---

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

// Hilfs-Arrays für die Logik (Bleiben für indexbasierten Zugriff erhalten)
static const long MIN_POS[3] = {X_MIN_POS, Y_MIN_POS, Z_MIN_POS};
static const long MAX_POS[3] = {X_MAX_POS, Y_MAX_POS, Z_MAX_POS};
static const float MAX_SPEED_LIMITS[3] = {X_MAX_SPEED, Y_MAX_SPEED, Z_MAX_SPEED};

#define MAX_STEPS_X        X_MAX_POS  // Legacy Support
#define DEFAULT_MAX_SPEED  1000.0
#define DEFAULT_ACCEL      500.0

#endif