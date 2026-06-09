#include <Arduino.h>
#include <Wire.h>
#include <Servo.h>
#include "i2c_protocol.h"

// Pin-Belegung laut Richtlinie (D7 bis D12)
const uint8_t SERVO_PINS[6] = {7, 8, 9, 10, 11, 12};
Servo servos[6];

uint16_t tracked_pwm[6] = {1500, 1500, 1500, 1500, 1500, 1500};

// Globale Variablen für I2C und Debugging
bool debug_enabled = false;
volatile bool new_servo_command = false;
volatile ServoCommand active_servo_cmd;

void onI2CReceive(int numBytes);
void onI2CRequest();
void handleSerialCommands();
void executeServoCommand(ServoCommand cmd);
void printNanoHelp();

void setup() {
  // Initialisiere die 6 Servos
  for (int i = 0; i < 6; i++) {
    servos[i].attach(SERVO_PINS[i]);
    servos[i].writeMicroseconds(1500); // Standardmäßig in die Mitte (90 Grad) fahren
  }

  // I2C Bus als Slave initialisieren
  Wire.begin(I2C_ADDR_NANO);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  Serial.begin(9600);
  Serial.println(F("=== Arduino Nano: Servo & Sensor Slave ==="));
  Serial.println(F("Tippe 'h' fuer die lokale Servo-Befehlsuebersicht."));
  Serial.println(F("Tippe 'debug' ein, um Meldungen AN/AUS zu schalten."));
}

void loop() {
  // Lokale serielle Befehle verarbeiten
  handleSerialCommands();

  // Wenn ein I2C-Befehl vom Master kam
  if (new_servo_command) {
    new_servo_command = false;

    // Lokale Kopie der volatile Struktur ziehen
    ServoCommand cmd_copy;
    cmd_copy.servo_num = active_servo_cmd.servo_num;
    cmd_copy.pwm_value = active_servo_cmd.pwm_value;

    executeServoCommand(cmd_copy);
  }
}

void executeServoCommand(ServoCommand cmd) {
  if (debug_enabled) {
    Serial.print(F("[Execute Nano] Servo: ")); Serial.print(cmd.servo_num);
    Serial.print(F(" | PWM-Wert: ")); Serial.println(cmd.pwm_value);
  }

  // Sicherheitsprüfung für den Servo-Index
  if (cmd.servo_num >= 6) {
    if (debug_enabled) Serial.println(F("  -> ABGELEHNT: Servo-Nummer existiert nicht (0-5)!"));
    return;
  }

  // Sicherheitsprüfung für den PWM-Bereich (typisch 500 bis 2500 µs)
  if (cmd.pwm_value < 500 || cmd.pwm_value > 2500) {
    if (debug_enabled) Serial.println(F("  -> ABGELEHNT: PWM-Wert ausserhalb des sicheren Bereichs (500-2500)!"));
    return;
  }

  // Servo-Pulsweite aktualisieren
  servos[cmd.servo_num].writeMicroseconds(cmd.pwm_value);
  tracked_pwm[cmd.servo_num] = cmd.pwm_value;
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    if (input.equalsIgnoreCase("h")) { printNanoHelp(); return; }
    if (input.equalsIgnoreCase("debug")) {
      debug_enabled = !debug_enabled;
      Serial.print(F("-> NANO DEBUG: ")); Serial.println(debug_enabled ? F("AN") : F("AUS"));
      return;
    }

    // Lokaler Parser Format: [ServoNum],[PWM] -> z.B. 0,1800
    int commaIdx = input.indexOf(',');
    if (commaIdx != -1) {
      uint8_t s_num = input.substring(0, commaIdx).toInt();
      uint16_t pwm = input.substring(commaIdx + 1).toInt();

      ServoCommand local_cmd = {s_num, pwm};
      executeServoCommand(local_cmd);
    }
  }
}

void printNanoHelp() {
  Serial.println(F("\n=================== NANO BEFEHLE ==================="));
  Serial.println(F("Format: [ServoNummer],[PWM-Wert]"));
  Serial.println(F("  0,1500    -> Setzt Servo 0 (Pin D7) auf die Mittelstellung"));
  Serial.println(F("  5,2000    -> Setzt Servo 5 (Pin D12) weit nach rechts"));
  Serial.println(F("Grenzwerte:"));
  Serial.println(F("  Servos:   0 bis 5"));
  Serial.println(F("  PWM:      500 bis 2500 Mikrosekunden"));
  Serial.println(F("Systembefehle:"));
  Serial.println(F("  debug     -> Schaltet Live-Meldungen AN/AUS"));
  Serial.println(F("==================================================="));
}

void onI2CReceive(int numBytes) {
  if (numBytes >= (int)sizeof(ServoCommand)) {
    byte buffer[sizeof(ServoCommand)];
    for (int i = 0; i < (int)sizeof(ServoCommand); i++) {
      buffer[i] = Wire.read();
    }
    ServoCommand* cmd_ptr = (ServoCommand*)buffer;
    active_servo_cmd.servo_num = cmd_ptr->servo_num;
    active_servo_cmd.pwm_value = cmd_ptr->pwm_value;
    new_servo_command = true;
  }
  while (Wire.available()) { Wire.read(); }
}

void onI2CRequest() {
  ServoStatus status;
  for(int i = 0; i < 6; i++) {
    status.current_pwm[i] = tracked_pwm[i];
  }
  Wire.write((uint8_t*)&status, sizeof(ServoStatus));
}