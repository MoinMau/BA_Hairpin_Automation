#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>

// I2C Slave-Adressen
#define I2C_ADDR_NANO        0x32  // Adresse für Slave 2 (Servos & Sensoren)
#define I2C_ADDR_UNO         0x33  // Adresse für Slave 1 (Schrittmotoren)

// --- Schrittmotor-Definitionen (Uno) ---
#define AXIS_X               0
#define AXIS_Y               1
#define AXIS_Z               2

#define MOVE_TYPE_RELATIVE   1
#define MOVE_TYPE_ABSOLUTE   2
#define MOVE_TYPE_TIMED      3
#define MOVE_TYPE_VIBRATE    4
#define MOVE_TYPE_STOP       5  
#define MOVE_TYPE_HOMING     6  

struct StepperCommand {
  uint8_t axis;         
  uint8_t move_type;    
  int32_t parameter1;   
  int16_t parameter2;   
} __attribute__((packed));

// --- NEU: Servo-Definitionen (Nano) ---
// Struktur ist exakt 3 Bytes groß (kein Padding dank packed)
struct ServoCommand {
  uint8_t servo_num;    // Index des Servos (z.B. 0 bis 5 für die 6 Servos)
  uint16_t pwm_value;   // PWM-Wert / Pulsweite (typischerweise zwischen 500 und 2500 Mikrosekunden)
} __attribute__((packed));

#endif // I2C_PROTOCOL_H