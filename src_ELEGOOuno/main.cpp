#include <Arduino.h>
#include <Wire.h>
#include <AccelStepper.h>
#include "i2c_protocol.h"

#define STEPPER_X_STP 2   
#define STEPPER_X_DIR 5   
#define STEPPER_Y_STP 3   
#define STEPPER_Y_DIR 6   
#define STEPPER_Z_STP 4   
#define STEPPER_Z_DIR 7   
#define STEPPER_EN 8  

#define LED_PIN 13

#define ENDSTOP_X 9       
#define MAX_STEPS_X 8000  

AccelStepper stepper_x(1, STEPPER_X_STP, STEPPER_X_DIR);
AccelStepper stepper_y(1, STEPPER_Y_STP, STEPPER_Y_DIR);
AccelStepper stepper_z(1, STEPPER_Z_STP, STEPPER_Z_DIR);

#define DEFAULT_MAX_SPEED 1000.0      
#define DEFAULT_ACCEL 500.0    

volatile bool new_command_received = false;
volatile StepperCommand active_cmd;

// Zeitsteuerung für TIMED
unsigned long timed_move_start[3] = {0, 0, 0};
unsigned long timed_move_duration[3] = {0, 0, 0};
bool timed_move_active[3] = {false, false, false};

// --- NEU: Variablen für den Vibrationsmodus ---
bool vibrate_active[3] = {false, false, false};
int32_t vibrate_amplitude[3] = {0, 0, 0};     // Ausschlag in Schritten
unsigned long vibrate_half_period[3] = {0, 0, 0}; // Zeit für eine Richtung in ms
unsigned long vibrate_last_toggle[3] = {0, 0, 0}; // Letzter Richtungswechsel
bool vibrate_direction[3] = {false, false, false}; // true = vor, false = zurück

bool test_mode_active = false; 

enum HomingState { HOMING_IDLE, HOMING_SEARCHING, HOMING_REBOUND };
HomingState x_homing_state = HOMING_IDLE;

void onI2CReceive(int numBytes);
void handleSerialDebug();
void executeStepperCommand(StepperCommand cmd);
void printHelp();
void startHomingX();

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(STEPPER_EN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(STEPPER_EN, LOW); 
  pinMode(ENDSTOP_X, INPUT_PULLUP);
  
  stepper_x.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_x.setAcceleration(DEFAULT_ACCEL);
  stepper_y.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_y.setAcceleration(DEFAULT_ACCEL);
  stepper_z.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_z.setAcceleration(DEFAULT_ACCEL);
  
  Wire.begin(I2C_ADDR_UNO);
  Wire.onReceive(onI2CReceive);
  
  Serial.begin(9600);
  Serial.println(F("=== Arduino Uno: Stepper Control & Vibration ==="));
  Serial.println(F("Sende 't' fuer den TESTMODUS."));
}

void loop() {
  // 1. Endschalter-Notstopp
  if (digitalRead(ENDSTOP_X) == LOW && x_homing_state == HOMING_IDLE) {
    if (stepper_x.speed() < 0 || stepper_x.distanceToGo() < 0) {
      stepper_x.stop();
      vibrate_active[AXIS_X] = false; // Vibration stoppen bei Kollision
      stepper_x.setCurrentPosition(0); 
      if (test_mode_active) Serial.println(F("NOTSTOPP: Endstopp X ausgeloest!"));
    }
  }

  // 2. TIMED Modus Überwachung
  for (int i = 0; i < 3; i++) {
    if (timed_move_active[i]) {
      if (millis() - timed_move_start[i] >= timed_move_duration[i]) {
        timed_move_active[i] = false;
        if (i == 0) stepper_x.stop();
        if (i == 1) stepper_y.stop();
        if (i == 2) stepper_z.stop();
      }
    }
  }

  // 3. Vibrationsmodus Ablaufsteuerung (Jetzt hochpraezise in Mikrosekunden!)
  for (int i = 0; i < 3; i++) {
    if (vibrate_active[i]) {
      if (micros() - vibrate_last_toggle[i] >= vibrate_half_period[i]) {
        vibrate_last_toggle[i] = micros();
        vibrate_direction[i] = !vibrate_direction[i]; // Richtung umkehren
        
        long steps = vibrate_direction[i] ? vibrate_amplitude[i] : -vibrate_amplitude[i];
        
        if (i == 0) {
          stepper_x.setMaxSpeed(4000); // Erhöhter Max-Speed für Mikrosekunden-Takt
          stepper_x.move(steps);
        } else if (i == 1) {
          stepper_y.setMaxSpeed(4000);
          stepper_y.move(steps);
        } else if (i == 2) {
          stepper_z.setMaxSpeed(4000);
          stepper_z.move(steps);
        }
      }
    }
  }

  // 4. Homing Ablaufsteuerung
  if (x_homing_state == HOMING_SEARCHING) {
    if (digitalRead(ENDSTOP_X) == LOW) { 
      stepper_x.stop();
      stepper_x.setMaxSpeed(200); 
      stepper_x.move(200); 
      x_homing_state = HOMING_REBOUND;
    } else {
      stepper_x.runSpeed(); 
    }
  } 
  else if (x_homing_state == HOMING_REBOUND) {
    stepper_x.run();
    if (stepper_x.distanceToGo() == 0) {
      stepper_x.setCurrentPosition(0);
      stepper_x.setMaxSpeed(DEFAULT_MAX_SPEED);
      x_homing_state = HOMING_IDLE;
      if (test_mode_active) Serial.println(F("X-Kalibrierung ERFOLGREICH!"));
    }
  } 
  else {
    // Normaler Lauf / Vibrations-Lauf für X
    if (timed_move_active[AXIS_X]) stepper_x.runSpeed(); else stepper_x.run();
  }

  // Normaler Lauf / Vibrations-Lauf für Y und Z
  if (timed_move_active[AXIS_Y]) stepper_y.runSpeed(); else stepper_y.run();
  if (timed_move_active[AXIS_Z]) stepper_z.runSpeed(); else stepper_z.run();
  
  handleSerialDebug();
  
  if (new_command_received) {
    new_command_received = false;
    
    Serial.println(F("\n[Uno Loop] Verarbeite neuen I2C-Befehl..."));
    Serial.print(F("  -> Empfangene Achse (Raw): ")); Serial.println(active_cmd.axis);
    Serial.print(F("  -> Bewegungstyp (Raw): ")); Serial.println(active_cmd.move_type);
    Serial.print(F("  -> Parameter 1: ")); Serial.println(active_cmd.parameter1);
    Serial.print(F("  -> Parameter 2: ")); Serial.println(active_cmd.parameter2);

    // Wir kopieren und fuehren den Befehl JETZT aus, 
    // um zu sehen, ob executeStepperCommand() ihn ablehnt!
    StepperCommand cmd_copy;
    cmd_copy.axis = active_cmd.axis;
    cmd_copy.move_type = active_cmd.move_type;
    cmd_copy.parameter1 = active_cmd.parameter1;
    cmd_copy.parameter2 = active_cmd.parameter2;
    
    executeStepperCommand(cmd_copy);
  }
  
  // Live-Positionsanzeige
  static unsigned long last_status = 0;
  if (test_mode_active && (millis() - last_status >= 200)) {
    bool is_moving = (stepper_x.distanceToGo() != 0 || stepper_y.distanceToGo() != 0 || stepper_z.distanceToGo() != 0 || 
                      timed_move_active[0] || timed_move_active[1] || timed_move_active[2] || 
                      vibrate_active[0] || vibrate_active[1] || vibrate_active[2] || x_homing_state != HOMING_IDLE);
                      
    if (is_moving) {
      Serial.print(F("LIVE-POS -> X:")); Serial.print(stepper_x.currentPosition());
      Serial.print(F(" | Y:")); Serial.print(stepper_y.currentPosition());
      Serial.print(F(" | Z:")); Serial.println(stepper_z.currentPosition());
    }
    last_status = millis();
  }
}

void startHomingX() {
  if (test_mode_active) Serial.println(F("Suche Endstopp..."));
  x_homing_state = HOMING_SEARCHING;
  timed_move_active[AXIS_X] = false;
  vibrate_active[AXIS_X] = false;
  stepper_x.setSpeed(-400); 
}

void executeStepperCommand(StepperCommand cmd) {
  Serial.println(F("[Uno Execute] Pruefe Grenzwerte..."));
  
  if (cmd.axis == AXIS_X && x_homing_state == HOMING_IDLE && cmd.move_type != MOVE_TYPE_VIBRATE) {
    if (cmd.move_type == MOVE_TYPE_ABSOLUTE && (cmd.parameter1 < 0 || cmd.parameter1 > MAX_STEPS_X)) {
      Serial.println(F("  -> ABGELEHNT: ABS-Ziel ausserhalb Software-Limit X (0-8000)!"));
      return;
    }
    if (cmd.move_type == MOVE_TYPE_RELATIVE) {
      long preview = stepper_x.currentPosition() + cmd.parameter1;
      if (preview < 0 || preview > MAX_STEPS_X) {
        Serial.println(F("  -> ABGELEHNT: REL-Fahrt verletzt Software-Limit X!"));
        return;
      }
    }
  }

  AccelStepper* target = nullptr;
  if (cmd.axis == AXIS_X) { target = &stepper_x; Serial.println(F("  -> Achse X gewaehlt")); }
  else if (cmd.axis == AXIS_Y) { target = &stepper_y; Serial.println(F("  -> Achse Y gewaehlt")); }
  else if (cmd.axis == AXIS_Z) { target = &stepper_z; Serial.println(F("  -> Achse Z gewaehlt")); }
  
  if (target == nullptr) {
    Serial.println(F("  -> ABGELEHNT: target ist NULL (Achsen-ID ungueltig)!"));
    return;
  }
  
  // Alle anderen Bewegungsmodi für diese Achse zurücksetzen
  timed_move_active[cmd.axis] = false;
  vibrate_active[cmd.axis] = false;

  switch (cmd.move_type) {
    case MOVE_TYPE_RELATIVE: 
      target->setMaxSpeed(cmd.parameter2);
      target->move(cmd.parameter1);
      break;
    case MOVE_TYPE_ABSOLUTE: 
      target->setMaxSpeed(cmd.parameter2);
      target->moveTo(cmd.parameter1);
      break;
    case MOVE_TYPE_TIMED:    
      target->setSpeed(cmd.parameter2); 
      timed_move_duration[cmd.axis] = cmd.parameter1; 
      timed_move_start[cmd.axis] = millis();
      timed_move_active[cmd.axis] = true;
      break;
    case MOVE_TYPE_VIBRATE: // --- NEU: Vibrations-Setup ---
      if (cmd.parameter2 <= 0 || cmd.parameter1 <= 0) {
        target->stop(); // Frequenz oder Amplitude 0 -> Stoppen
        if (test_mode_active) Serial.println(F("Vibration gestoppt."));
      } else {
        vibrate_amplitude[cmd.axis] = cmd.parameter1;
        // Halbe Periodendauer in ms berechnen: (1000 ms / Frequenz) / 2
        vibrate_half_period[cmd.axis] = (1000 / cmd.parameter2) / 2;
        if (vibrate_half_period[cmd.axis] < 1) vibrate_half_period[cmd.axis] = 1; // Schutz vor Division durch 0
        
        vibrate_last_toggle[cmd.axis] = millis();
        vibrate_direction[cmd.axis] = true;
        vibrate_active[cmd.axis] = true;
        
        // Ersten Impuls direkt abfeuern
        target->setMaxSpeed(2000);
        target->move(vibrate_amplitude[cmd.axis]);
      }
      break;
      case MOVE_TYPE_FREEZE: 
      target->stop(); 
      target->setCurrentPosition(target->currentPosition()); 
      timed_move_active[cmd.axis] = false;
      vibrate_active[cmd.axis] = false;
      
      // --- NEU: Abfangen der globalen Treiber-Zustaende via I2C ---
      if (cmd.parameter1 == 99) {
        digitalWrite(STEPPER_EN, HIGH); // disAll: Treiber AUS
        if (test_mode_active) Serial.println(F("I2C-Systemmeldung: Motoren STROMLOS (disAll)"));
      } else {
        digitalWrite(STEPPER_EN, LOW);  // enAll / Normaler Freeze: Treiber AN
        if (test_mode_active) Serial.println(F("I2C-Systemmeldung: Motoren SCHARF (enAll)"));
      }
      break;
  }
}

void handleSerialDebug() {
  if (Serial.available() > 0) {
    char ch = Serial.peek();
    if (ch == 't' || ch == 'T') {
      Serial.read(); 
      test_mode_active = !test_mode_active;
      Serial.println(F("\n-------------------------------------------"));
      Serial.print(F("TESTMODUS: ")); Serial.println(test_mode_active ? F("AKTIVIERT") : F("DEAKTIVIERT"));
      Serial.println(F("-------------------------------------------"));
      if (test_mode_active) printHelp();
      return;
    }
    
    if (!test_mode_active) {
      while(Serial.available()) Serial.read();
      return;
    }
    
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;
    if (input.equalsIgnoreCase("h")) { printHelp(); return; }
    if (input.equalsIgnoreCase("enAll")) { digitalWrite(STEPPER_EN, LOW); Serial.println(F("Treiber AN")); return; }
    if (input.equalsIgnoreCase("disAll")) { digitalWrite(STEPPER_EN, HIGH); Serial.println(F("Treiber AUS")); return; }
    if (input.equalsIgnoreCase("homeX")) { startHomingX(); return; }

    int firstComma = input.indexOf(',');
    int secondComma = input.indexOf(',', firstComma + 1);
    int thirdComma = input.indexOf(',', secondComma + 1);

    if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
      String axisStr = input.substring(0, firstComma);
      String typeStr = input.substring(firstComma + 1, secondComma);
      int32_t p1 = input.substring(secondComma + 1, thirdComma).toInt();
      int16_t p2 = input.substring(thirdComma + 1).toInt();

      axisStr.toUpperCase(); axisStr.trim();
      typeStr.toUpperCase(); typeStr.trim();

      StepperCommand cmd;
      if (axisStr == "STP_X") cmd.axis = AXIS_X;
      else if (axisStr == "STP_Y") cmd.axis = AXIS_Y;
      else if (axisStr == "STP_Z") cmd.axis = AXIS_Z;
      else { return; }

      if (typeStr == "REL") cmd.move_type = MOVE_TYPE_RELATIVE;
      else if (typeStr == "ABS") cmd.move_type = MOVE_TYPE_ABSOLUTE;
      else if (typeStr == "TIMED") cmd.move_type = MOVE_TYPE_TIMED;
      else if (typeStr == "VIB") cmd.move_type = MOVE_TYPE_VIBRATE;
      else if (typeStr == "FREEZE") cmd.move_type = MOVE_TYPE_FREEZE; // NEU: FREEZE über Seriell
      else { return; }

      cmd.parameter1 = p1;
      cmd.parameter2 = p2;

      Serial.print(F("Fuehre aus -> ")); Serial.print(axisStr);
      Serial.print(F(" | Typ: ")); Serial.print(typeStr);
      Serial.print(F(" | Amplitude: ")); Serial.print(p1);
      Serial.print(F(" | Frequenz: ")); Serial.print(p2); Serial.println(F(" Hz"));

      executeStepperCommand(cmd);
    }
  }
}

void printHelp() {
  Serial.println(F("Format: [STP_Achse],[Typ],[Param1],[Param2]"));
  Serial.println(F("  STP_X,REL,1000,600      -> Relative Schritte"));
  Serial.println(F("  STP_Y,ABS,2000,500      -> Absolute Position"));
  Serial.println(F("  STP_Z,TIMED,3000,-400   -> Fahrt auf Zeit"));
  Serial.println(F("  STP_X,VIB,5,60          -> Vibration (5 Steps Amplitude, 60 Hz)"));
  Serial.println(F("  STP_X,FREEZE,0,0        -> Stoppt Achse X SOFORT und loggt Position"));
  Serial.println(F("Spezialbefehle:"));
  Serial.println(F("  homeX  -> Startet X-Kalibrierung am Endstopp"));
  Serial.println(F("  enAll  -> Treiber AN | disAll -> Treiber AUS | t -> Beenden"));
}

void onI2CReceive(int numBytes) {
  // Sobald überhaupt Bytes reinkommen, lassen wir die LED umschalten!
  if (numBytes > 0) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); 
    
    // Ab hier folgt dein normaler, bestehender Code:
    if (numBytes >= (int)sizeof(StepperCommand)) {
      byte buffer[sizeof(StepperCommand)];
      for (int i = 0; i < (int)sizeof(StepperCommand); i++) {
        buffer[i] = Wire.read();
      }
      StepperCommand* cmd_ptr = (StepperCommand*)buffer;
      active_cmd.axis = cmd_ptr->axis;
      active_cmd.move_type = cmd_ptr->move_type;
      active_cmd.parameter1 = cmd_ptr->parameter1;
      active_cmd.parameter2 = cmd_ptr->parameter2;
      new_command_received = true; 
    }
  }
  while (Wire.available()) { Wire.read(); }
}