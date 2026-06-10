#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>

// I2C Slave-Adressen
#define I2C_ADDR_NANO        0x32  
#define I2C_ADDR_UNO         0x33  

// --- NEU: Anforderungs-IDs für gezielte Nano-Abfragen ---
#define REQ_NANO_SERVOS      0x01  // Fragt nur die Servo-Werte (0-1000) ab
#define REQ_NANO_SENSORS     0x02  // Fragt Shunt, MH-Sensoren und Analog-Pins ab

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

struct StepperStatus {
  int32_t current_pos_x; 
  int32_t current_pos_y;
  int32_t current_pos_z;
  uint8_t homing_active; 
} __attribute__((packed));


// --- Servo- & Sensor-Definitionen (Nano) ---
struct ServoCommand {
  uint8_t servo_num;    
  uint16_t pwm_value;   // Wird nun als Wert von 0 bis 1000 interpretiert!
} __attribute__((packed));

// Status-Struktur für Servos (Werte von 0 bis 1000)
struct ServoStatus {
  uint16_t current_val[6]; 
} __attribute__((packed));

// NEU: Status-Struktur für Sensoren (Exakt 12 Bytes)
struct SensorStatus {
  uint16_t shunt_raw;       // Shunt-Widerstand (A0)
  uint16_t mh_d2_state;     // Digitaler Zustand MH-Sensor (D2)
  uint16_t mh_a7_raw;       // Analoger Zustand MH-Sensor (A7)
  uint16_t analog_a1;       // Freie Analog-Pins für Spannungen
  uint16_t analog_a2;
  uint16_t analog_a3;
} __attribute__((packed));

#endif // I2C_PROTOCOL_H