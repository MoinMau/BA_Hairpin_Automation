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

void playTone(uint16_t freqHz, uint16_t durationMs) {
  if (freqHz == 0) {
    delay(durationMs);
    return;
  }

  const uint32_t halfPeriodUs = 1000000UL / (freqHz * 2UL);
  const uint32_t totalCycles = (uint32_t)freqHz * durationMs / 1000UL;

  for (uint32_t i = 0; i < totalCycles; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(halfPeriodUs);
  }
}

void loop() {
  // Simple melody on X axis
  const uint16_t notes[] = {523, 659, 784, 659, 523, 0, 523, 784, 988};
  const uint16_t lengths[] = {200, 200, 250, 200, 300, 120, 200, 250, 350};
  const size_t count = sizeof(notes) / sizeof(notes[0]);

  for (size_t i = 0; i < count; i++) {
    playTone(notes[i], lengths[i]);
    delay(40);
  }

  delay(1000);
}