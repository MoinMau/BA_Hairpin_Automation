#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>

#define I2C_ADDR_UNO         0x33

// Befehls-IDs
#define CMD_STEPPER_ROTATE   0x10
#define CMD_STEPPER_HOMING   0x12

// Achsen-Definitionen
#define AXIS_X               0
#define AXIS_Y               1
#define AXIS_Z               2

// Bewegungstypen
#define MOVE_TYPE_RELATIVE   1
#define MOVE_TYPE_ABSOLUTE   2
#define MOVE_TYPE_TIMED      3
#define MOVE_TYPE_VIBRATE    4
#define MOVE_TYPE_FREEZE     5

// Durch das "packed"-Attribut zwingen wir BEIDE Compiler (ESP32 & Uno)
// die Struktur exakt Bit für Bit ohne Füllbytes im Speicher abzulegen!
struct StepperCommand {
  uint8_t axis;         
  uint8_t move_type;    
  int32_t parameter1;   
  int16_t parameter2;   
} __attribute__((packed)); // <-- HIER DIE ÄNDERUNG!

#endif // I2C_PROTOCOL_H