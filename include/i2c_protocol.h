#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

// I2C Adressen
#define I2C_ADDR_UNO 0x33      // Arduino Uno (Stepper)
#define I2C_ADDR_NANO 0x32     // Arduino Nano (Servo)

// I2C Befehle
#define CMD_STEPPER_ROTATE 0x10  // Stepper X Schritte rotieren
#define CMD_SERVO_SET_ANGLE 0x20 // Servo auf Winkel setzen

// Datenstrukturen für I2C
struct StepperCommand {
  uint8_t cmd;           // CMD_STEPPER_ROTATE
  int16_t steps;         // Anzahl Schritte (+ = CW, - = CCW)
  uint8_t speed;         // Geschwindigkeit 0-255
};

struct ServoCommand {
  uint8_t cmd;           // CMD_SERVO_SET_ANGLE
  uint8_t servo_id;      // 0-5 für D7-D12
  uint8_t angle;         // 0-180 Grad
};

#endif
