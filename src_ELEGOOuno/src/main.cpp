#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

// CNC-Shield Pin-Belegung für Stepper
#define STEPPER_STEP 2   // X.STP
#define STEPPER_DIR 5    // X.DIR

// I2C
#define I2C_ADDR 0x33
#define I2C_SDA A4
#define I2C_SCL A5

// Stepper-Zustand
struct StepperState {
  int steps_remaining;
  uint8_t speed;
  unsigned long last_step_time;
  int current_direction; // 1 oder -1
};

StepperState stepper = {0, 100, 0, 1};

void setup() {
  // Pins initialisieren
  pinMode(STEPPER_STEP, OUTPUT);
  pinMode(STEPPER_DIR, OUTPUT);
  digitalWrite(STEPPER_STEP, LOW);
  digitalWrite(STEPPER_DIR, LOW);
  
  // I2C Slave Initialisierung
  Wire.begin(I2C_ADDR);
  Wire.onReceive(onI2CReceive);
  
  Serial.begin(9600);
  Serial.println("=== Arduino Uno: Stepper Control ===");
  Serial.print("I2C Adresse: 0x");
  Serial.println(I2C_ADDR, HEX);
}

void loop() {
  // Nicht-blockierendes Stepper-Stepping mit millis()
  if (stepper.steps_remaining > 0) {
    // Berechne Verzögerung basierend auf Speed (0-255)
    // Speed 100 = ~1000us zwischen Schritten
    unsigned long delay_us = map(stepper.speed, 0, 255, 5000, 500);
    
    if (millis() * 1000 - stepper.last_step_time >= delay_us) {
      // Schritt ausführen
      digitalWrite(STEPPER_STEP, HIGH);
      delayMicroseconds(10);
      digitalWrite(STEPPER_STEP, LOW);
      delayMicroseconds(10);
      
      stepper.steps_remaining--;
      stepper.last_step_time = millis() * 1000;
      
      if (stepper.steps_remaining == 0) {
        Serial.println("✓ Stepper: Bewegung beendet");
      }
    }
  }
}

// I2C Interrupt Handler
void onI2CReceive(int numBytes) {
  if (numBytes >= sizeof(StepperCommand)) {
    byte buffer[sizeof(StepperCommand)];
    
    for (int i = 0; i < sizeof(StepperCommand); i++) {
      buffer[i] = Wire.read();
    }
    
    StepperCommand* cmd = (StepperCommand*)buffer;
    
    if (cmd->cmd == CMD_STEPPER_ROTATE) {
      // Richtung setzen
      if (cmd->steps > 0) {
        digitalWrite(STEPPER_DIR, HIGH); // Uhrzeigersinn
        stepper.current_direction = 1;
      } else {
        digitalWrite(STEPPER_DIR, LOW);  // Gegen Uhrzeigersinn
        stepper.current_direction = -1;
      }
      
      // Bewegung starten
      stepper.steps_remaining = abs(cmd->steps);
      stepper.speed = cmd->speed;
      stepper.last_step_time = millis() * 1000;
      
      Serial.print("Befehl erhalten: ");
      Serial.print(cmd->steps);
      Serial.print(" Schritte, Speed: ");
      Serial.println(cmd->speed);
    }
  }
  
  // Restliche Bytes auslesen
  while (Wire.available()) {
    Wire.read();
  }
}
