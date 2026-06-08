#include <Arduino.h>
#include <Servo.h>

const uint8_t LED_PIN = 13;
const uint8_t SERVO_PIN = 10;

Servo servo;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  servo.attach(SERVO_PIN);
  servo.write(90);  // Center position
}

void loop() {
  // LED blink
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
  
  // Servo sweep
  for (int angle = 0; angle <= 180; angle += 5) {
    servo.write(angle);
    delay(50);
  }
  
  for (int angle = 180; angle >= 0; angle -= 5) {
    servo.write(angle);
    delay(50);
  }
}

