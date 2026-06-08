#include <Arduino.h>
#include <Servo.h>

const uint8_t LED_PIN = 13;
const uint8_t SERVO_PIN = 10;

Servo servo;

void setup() {
    Serial.begin(115200);
    analogReference(INTERNAL);
  pinMode(LED_PIN, OUTPUT);
  servo.attach(SERVO_PIN);
  servo.write(90);  // Center position
}

void loop() {
  // LED blink
  digitalWrite(LED_PIN, HIGH);
  servo.write(0);  // Center position
  Serial.println(analogRead(A0));  // Simulate some work
  delay(500);
  digitalWrite(LED_PIN, LOW);
  servo.write(180);  // Center position
  Serial.println(analogRead(A0));  // Simulate some work
  delay(500);
  digitalWrite(LED_PIN, LOW);
  servo.write(180);  // Center position
  Serial.println(analogRead(A0));  // Simulate some work
  delay(500);
}

