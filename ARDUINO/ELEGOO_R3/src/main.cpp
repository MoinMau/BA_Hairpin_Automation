#include <Arduino.h>

// CNC Shield V3 defaults (X axis)
const uint8_t STEP_PIN = 2; // X.STEP
const uint8_t DIR_PIN = 5;  // X.DIR
const uint8_t EN_PIN = 8;   // Enable (active LOW)

void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);

  digitalWrite(EN_PIN, LOW); // enable drivers
  digitalWrite(DIR_PIN, LOW);
}

void stepPulse(uint16_t steps, uint16_t stepDelayUs) {
  for (uint16_t i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(stepDelayUs);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(stepDelayUs);
  }
}

void loop() {
  stepPulse(400, 600); // move one direction
  delay(500);

  digitalWrite(DIR_PIN, !digitalRead(DIR_PIN)); // reverse
  stepPulse(400, 600);
  delay(500);
}