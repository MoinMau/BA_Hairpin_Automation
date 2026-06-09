#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <Arduino.h>

// I2C Adressen
#define I2C_ADDR_UNO 0x33      // Arduino Uno (Stepper)
#define I2C_ADDR_NANO 0x32     // Arduino Nano (Servo)

// Achsen-IDs
#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 2

// Move-Typen (Move-Type)
#define MOVE_TYPE_RELATIVE   1  // Direkt Schritte (steps, speed) -> Richtung über Vorzeichen von steps
#define MOVE_TYPE_ABSOLUTE   2  // Move to Position (target_position, speed)
#define MOVE_TYPE_TIMED      3  // Move mit Speed für X Millisekunden (speed, duration_ms) -> Richtung über Vorzeichen von speed

#define CMD_STEPPER_ROTATE   0x10  
#define CMD_STEPPER_HOMING   0x12  // NEU: Kalibrierungsfahrt starten

// Datenstruktur für I2C
struct StepperCommand {
  uint8_t axis;          // AXIS_X, AXIS_Y, AXIS_Z
  uint8_t move_type;     // MOVE_TYPE_RELATIVE, MOVE_TYPE_ABSOLUTE, MOVE_TYPE_TIMED
  int32_t parameter1;    // steps (rel) / target_pos (abs) / duration_ms (timed)
  int16_t parameter2;    // speed (für rel & abs) / speed (mit Vorzeichen für timed)
};

#endif