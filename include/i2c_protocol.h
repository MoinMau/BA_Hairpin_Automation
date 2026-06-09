#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>

// I2C Slave-Adressen
#define I2C_ADDR_NANO        0x32  
#define I2C_ADDR_UNO         0x33  

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

// NEU: Status-Struktur vom Uno (Exakt 13 Bytes)
struct StepperStatus {
  int32_t current_pos_x; // Aktuelle Position in Schritten
  int32_t current_pos_y;
  int32_t current_pos_z;
  uint8_t homing_active; // Bitmaske oder Flag (0 = Idle, >0 = Kalibrierung läuft)
} __attribute__((packed));


// --- Servo-Definitionen (Nano) ---
struct ServoCommand {
  uint8_t servo_num;    
  uint16_t pwm_value;   
} __attribute__((packed));

// NEU: Status-Struktur vom Nano (Exakt 12 Bytes)
struct ServoStatus {
  uint16_t current_pwm[6]; // Aktuelle PWM-Werte der 6 Servos (6 * 2 Bytes)
} __attribute__((packed));

#endif // I2C_PROTOCOL_H