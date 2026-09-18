#include <Arduino.h>
#include <Wire.h>
#include <Servo.h>
#include "i2c_protocol.h"
#include "config_nano.h"

Servo servos[6];

// Globale Variablen für I2C und Debugging
bool debug_enabled = false;
volatile bool new_servo_command = false;
volatile ServoCommand active_servo_cmd;
volatile uint8_t current_request_mode = REQ_NANO_SERVOS; // Standard-Antwortmodus

// Wir speichern hier die normierten Werte (0 bis 1000)
uint16_t tracked_servo_values[6] = {DEFAULT_SERVO_VAL, DEFAULT_SERVO_VAL, DEFAULT_SERVO_VAL, DEFAULT_SERVO_VAL, DEFAULT_SERVO_VAL, DEFAULT_SERVO_VAL}; // Start in der Mitte

void onI2CReceive(int numBytes);
void onI2CRequest();
void handleSerialCommands();
void executeServoCommand(ServoCommand cmd);

void setup() {
  pinMode(PIN_MH_DIGITAL, INPUT); //
  
  // Initialisiere die 6 Servos
  for (int i = 0; i < 6; i++) {
    servos[i].attach(SERVO_PINS[i]); //
    // 500 entspricht im Mapping genau 1500us (Mitte)
    servos[i].writeMicroseconds(1500); 
  }

  Wire.begin(I2C_ADDR_NANO); //
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  Serial.begin(9600);
  Serial.println(F("=== Arduino Nano: Servo & Advanced Sensor Slave ==="));
}

void loop() {
  handleSerialCommands();

  if (new_servo_command) {
    noInterrupts();
    ServoCommand cmd_copy;
    cmd_copy.servo_num = active_servo_cmd.servo_num;
    cmd_copy.pwm_value = active_servo_cmd.pwm_value;
    new_servo_command = false;
    interrupts();

    executeServoCommand(cmd_copy);
  }
}

void executeServoCommand(ServoCommand cmd) {
  if (cmd.servo_num >= 6) return;

  // Begrenzung auf die individuellen Servo-Limits aus der Config
  cmd.pwm_value = constrain(cmd.pwm_value, SERVO_MIN_LIMITS[cmd.servo_num], SERVO_MAX_LIMITS[cmd.servo_num]);

  // Wert im Array für die Telemetrie merken (0-1000)
  tracked_servo_values[cmd.servo_num] = cmd.pwm_value;

  // Richtungsumkehr (SERVO_INVERT aus config_nano.h)
  // false: 0 = 500us (min), 1000 = 2500us (max)
  // true:  0 = 2500us (max), 1000 = 500us (min)
  uint16_t mapped = cmd.pwm_value;
  if (SERVO_INVERT[cmd.servo_num]) {
    mapped = 1000 - mapped;
  }

  // Umrechnung (Mapping): 0 -> 500µs, 1000 -> 2500µs
  // Formel: 500 + (wert * 2000 / 1000) -> 500 + wert * 2
  uint16_t microseconds = 500 + (mapped * 2);

  servos[cmd.servo_num].writeMicroseconds(microseconds);

  if (debug_enabled) {
    Serial.print(F("[Servo] Nr: ")); Serial.print(cmd.servo_num);
    Serial.print(F(" | Wert: ")); Serial.print(cmd.pwm_value);
    Serial.print(F(" -> Gemappt auf: ")); Serial.print(microseconds); Serial.println(F(" us"));
    if (SERVO_INVERT[cmd.servo_num]) {
      Serial.print(F("  (Richtung invertiert - interner Wert: "));
      Serial.print(mapped); Serial.println(F(")"));
    }
  }
}

void onI2CReceive(int numBytes) {
  // FALL 1: Der Master schickt nur 1 Byte -> Das ist die Anforderungs-ID für den Status!
  if (numBytes == 1) {
    current_request_mode = Wire.read();
    return;
  }

  // FALL 2: Standard Steuerbefehl für die Servos
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
  if (current_request_mode == REQ_NANO_SERVOS) {
    ServoStatus sStatus;
    for (int i = 0; i < 6; i++) {
      sStatus.current_val[i] = tracked_servo_values[i];
    }
    Wire.write((uint8_t*)&sStatus, sizeof(ServoStatus));
  } 
  else if (current_request_mode == REQ_NANO_SENSORS) {
    SensorStatus snStatus;
    // Messwerte einlesen (Zustandsbasiert/Nicht-blockierend im Loop-Takt abgefragt)
    snStatus.shunt_raw = analogRead(PIN_SHUNT);        //
    snStatus.mh_d2_state = digitalRead(PIN_MH_DIGITAL); //
    snStatus.mh_a7_raw = analogRead(PIN_MH_ANALOG);    //
    
    // Spannungen an weiteren Analog-Pins auslesen
    snStatus.analog_a1 = analogRead(A1);
    snStatus.analog_a2 = analogRead(A2);
    snStatus.analog_a3 = analogRead(A3);

    Wire.write((uint8_t*)&snStatus, sizeof(SensorStatus));
  }
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("debug")) {
      debug_enabled = !debug_enabled;
      Serial.print(F("-> NANO DEBUG: ")); Serial.println(debug_enabled ? F("AN") : F("AUS"));
    }
  }
}