#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

// I2C Pins für ESP32
#define I2C_SDA 21
#define I2C_SCL 22
#define BAUD_RATE 115200

// Eingabepuffer
String inputBuffer = "";

void setup() {
  // Serial Initialisierung für Debug-Ausgabe
  Serial.begin(BAUD_RATE);
  delay(100);
  
  // I2C Master Initialisierung
  Wire.begin(I2C_SDA, I2C_SCL);
  
  Serial.println("=== ESP32 Test Setup ===");
  Serial.println("Befehle:");
  Serial.println("  s<steps> - Stepper rotieren (z.B. s100 oder s-50)");
  Serial.println("  v<id><angle> - Servo setzen (z.B. v0090 für Servo 0, 90°)");
  Serial.println("  ? - Hilfe anzeigen");
}

void loop() {
  // Seriellen Input verarbeiten
  if (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }
}

// Befehl von Serial Monitor verarbeiten
void processCommand(String cmd) {
  Serial.print("> ");
  Serial.println(cmd);
  
  if (cmd == "?") {
    printHelp();
    return;
  }
  
  if (cmd.startsWith("s")) {
    // Stepper Befehl: s<steps>
    int steps = cmd.substring(1).toInt();
    moveStepperViaI2C(steps);
  } 
  else if (cmd.startsWith("v")) {
    // Servo Befehl: v<id><angle> (z.B. v0090)
    if (cmd.length() >= 5) {
      int servo_id = cmd.substring(1, 2).toInt();
      int angle = cmd.substring(2, 5).toInt();
      moveServoViaI2C(servo_id, angle);
    } else {
      Serial.println("  Fehler: Format v<id><angle> (z.B. v0090)");
    }
  }
  else {
    Serial.println("  Unbekannter Befehl. ? für Hilfe.");
  }
}

// Stepper via I2C steuern (an Uno 0x33)
void moveStepperViaI2C(int steps) {
  StepperCommand cmd;
  cmd.cmd = CMD_STEPPER_ROTATE;
  cmd.steps = steps;
  cmd.speed = 100; // Standard-Geschwindigkeit
  
  Wire.beginTransmission(I2C_ADDR_UNO);
  Wire.write((uint8_t*)&cmd, sizeof(cmd));
  int result = Wire.endTransmission();
  
  if (result == 0) {
    Serial.print("  ✓ Stepper: ");
    Serial.print(steps);
    Serial.println(" Schritte");
  } else {
    Serial.println("  ✗ Fehler: Uno nicht erreichbar");
  }
}

// Servo via I2C steuern (an Nano 0x32)
void moveServoViaI2C(int servo_id, int angle) {
  // Bounds Check
  if (servo_id < 0 || servo_id > 5) {
    Serial.println("  Fehler: Servo-ID muss 0-5 sein");
    return;
  }
  if (angle < 0 || angle > 180) {
    Serial.println("  Fehler: Winkel muss 0-180 sein");
    return;
  }
  
  ServoCommand cmd;
  cmd.cmd = CMD_SERVO_SET_ANGLE;
  cmd.servo_id = servo_id;
  cmd.angle = angle;
  
  Wire.beginTransmission(I2C_ADDR_NANO);
  Wire.write((uint8_t*)&cmd, sizeof(cmd));
  int result = Wire.endTransmission();
  
  if (result == 0) {
    Serial.print("  ✓ Servo ");
    Serial.print(servo_id);
    Serial.print(": ");
    Serial.print(angle);
    Serial.println("°");
  } else {
    Serial.println("  ✗ Fehler: Nano nicht erreichbar");
  }
}

void printHelp() {
  Serial.println("\n=== Hilfe ===");
  Serial.println("Stepper rotieren:");
  Serial.println("  s100    - 100 Schritte im Uhrzeigersinn");
  Serial.println("  s-50    - 50 Schritte gegen Uhrzeigersinn");
  Serial.println("\nServo bewegen:");
  Serial.println("  v0090   - Servo 0 auf 90°");
  Serial.println("  v1000   - Servo 1 auf 0°");
  Serial.println("  v2180   - Servo 2 auf 180°");
  Serial.println();
}
