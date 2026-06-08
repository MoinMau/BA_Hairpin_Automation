#include <Arduino.h>
#include <Wire.h>
#include <Servo.h>
#include "i2c_protocol.h"

// Servo Pins: D7-D12 (D7=D7, D8=D8, D9=D9, D10=D10, D11=D11, D12=D12)
#define SERVO_PIN_0 7
#define SERVO_PIN_1 8
#define SERVO_PIN_2 9
#define SERVO_PIN_3 10
#define SERVO_PIN_4 11
#define SERVO_PIN_5 12

// I2C
#define I2C_ADDR 0x32
#define I2C_SDA A4
#define I2C_SCL A5

// Servo-Array
Servo servos[6];
int servo_pins[6] = {SERVO_PIN_0, SERVO_PIN_1, SERVO_PIN_2, SERVO_PIN_3, SERVO_PIN_4, SERVO_PIN_5};
int current_angles[6] = {90, 90, 90, 90, 90, 90}; // Start bei 90°

// Servo-Bewegungs-State
struct ServoState {
  int target_angle;
  int current_angle;
  unsigned long last_update;
  uint8_t servo_id;
  bool moving;
};

ServoState servo_states[6] = {
  {90, 90, 0, 0, false},
  {90, 90, 0, 1, false},
  {90, 90, 0, 2, false},
  {90, 90, 0, 3, false},
  {90, 90, 0, 4, false},
  {90, 90, 0, 5, false}
};

void setup() {
  // Servos initialisieren
  for (int i = 0; i < 6; i++) {
    servos[i].attach(servo_pins[i]);
    servos[i].write(90); // Neutral Position
  }
  
  // I2C Slave Initialisierung
  Wire.begin(I2C_ADDR);
  Wire.onReceive(onI2CReceive);
  
  Serial.begin(9600);
  Serial.println("=== Arduino Nano: Servo Control ===");
  Serial.print("I2C Adresse: 0x");
  Serial.println(I2C_ADDR, HEX);
  Serial.println("6 Servos auf Pins D7-D12");
}

void loop() {
  // Nicht-blockierende Servo-Bewegung mit millis()
  for (int i = 0; i < 6; i++) {
    if (servo_states[i].moving) {
      // Servo sanft zum Zielwinkel bewegen (ca. 5ms zwischen 1° Schritten)
      if (millis() - servo_states[i].last_update >= 20) {
        if (servo_states[i].current_angle < servo_states[i].target_angle) {
          servo_states[i].current_angle++;
        } else if (servo_states[i].current_angle > servo_states[i].target_angle) {
          servo_states[i].current_angle--;
        }
        
        servos[i].write(servo_states[i].current_angle);
        servo_states[i].last_update = millis();
        
        // Zielwinkel erreicht
        if (servo_states[i].current_angle == servo_states[i].target_angle) {
          servo_states[i].moving = false;
          Serial.print("✓ Servo ");
          Serial.print(i);
          Serial.print(" erreicht ");
          Serial.print(servo_states[i].target_angle);
          Serial.println("°");
        }
      }
    }
  }
}

// I2C Interrupt Handler
void onI2CReceive(int numBytes) {
  if (numBytes >= sizeof(ServoCommand)) {
    byte buffer[sizeof(ServoCommand)];
    
    for (int i = 0; i < sizeof(ServoCommand); i++) {
      buffer[i] = Wire.read();
    }
    
    ServoCommand* cmd = (ServoCommand*)buffer;
    
    if (cmd->cmd == CMD_SERVO_SET_ANGLE) {
      if (cmd->servo_id >= 0 && cmd->servo_id < 6) {
        if (cmd->angle >= 0 && cmd->angle <= 180) {
          servo_states[cmd->servo_id].target_angle = cmd->angle;
          servo_states[cmd->servo_id].moving = true;
          servo_states[cmd->servo_id].last_update = millis();
          
          Serial.print("Befehl erhalten: Servo ");
          Serial.print(cmd->servo_id);
          Serial.print(" -> ");
          Serial.print(cmd->angle);
          Serial.println("°");
        } else {
          Serial.println("Fehler: Winkel außerhalb 0-180");
        }
      } else {
        Serial.println("Fehler: Servo-ID außerhalb 0-5");
      }
    }
  }
  
  // Restliche Bytes auslesen
  while (Wire.available()) {
    Wire.read();
  }
}
