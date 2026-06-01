#include <Arduino.h>

const uint8_t kLedPin = 2;
const uint8_t kServoPin = 33;
const uint8_t kServoChannel = 0;
const uint32_t kServoFreqHz = 50;
const uint8_t kServoResolutionBits = 16;
const uint16_t kServoMinUs = 500;
const uint16_t kServoMaxUs = 2500;

void setup() {
  pinMode(kLedPin, OUTPUT);
  ledcSetup(kServoChannel, kServoFreqHz, kServoResolutionBits);
  ledcAttachPin(kServoPin, kServoChannel);
}

static uint32_t pulseUsToDuty(uint32_t pulse_us) {
  const uint32_t max_duty = (1u << kServoResolutionBits) - 1u;
  const uint32_t period_us = 1000000u / kServoFreqHz;
  return (pulse_us * max_duty) / period_us;
}

static void writeServoAngle(uint8_t angle_deg) {
  const uint32_t pulse_us = kServoMinUs +
      (static_cast<uint32_t>(angle_deg) * (kServoMaxUs - kServoMinUs)) / 180u;
  ledcWrite(kServoChannel, pulseUsToDuty(pulse_us));
}

void loop() {
  digitalWrite(kLedPin, HIGH);
  for (uint8_t angle = 0; angle <= 180; angle += 10) {
    writeServoAngle(angle);
    delay(300);
  }

  digitalWrite(kLedPin, LOW);
  for (int angle = 180; angle >= 0; angle -= 10) {
    writeServoAngle(static_cast<uint8_t>(angle));
    delay(300);
  }
}