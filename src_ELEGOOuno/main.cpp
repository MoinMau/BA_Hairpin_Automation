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

// Endstopp-Pin für X-Achse (CNC Shield V3 X-Limit Pin)
#define ENDSTOP_X 9       
// Maximaler Verfahrweg für die X-Achse nach der Kalibrierung
#define MAX_STEPS_X 8000  

AccelStepper stepper_x(1, STEPPER_X_STP, STEPPER_X_DIR);
AccelStepper stepper_y(1, STEPPER_Y_STP, STEPPER_Y_DIR);
AccelStepper stepper_z(1, STEPPER_Z_STP, STEPPER_Z_DIR);

#define DEFAULT_MAX_SPEED 1000.0      
#define DEFAULT_ACCEL 500.0    

volatile bool new_command_received = false;
volatile StepperCommand active_cmd;

// Zeitsteuerung für "Move mit Speed und Zeit"
unsigned long timed_move_start[3] = {0, 0, 0};
unsigned long timed_move_duration[3] = {0, 0, 0};
bool timed_move_active[3] = {false, false, false};

bool test_mode_active = false; 

// Zustände für die Kalibrierung
enum HomingState { HOMING_IDLE, HOMING_SEARCHING, HOMING_REBOUND };
HomingState x_homing_state = HOMING_IDLE;

void onI2CReceive(int numBytes);
void handleSerialDebug();
void executeStepperCommand(StepperCommand cmd);
void printHelp();
void startHomingX();

void setup() {
  pinMode(STEPPER_EN, OUTPUT);
  digitalWrite(STEPPER_EN, LOW); 
  
  pinMode(ENDSTOP_X, INPUT_PULLUP);
  
  stepper_x.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_x.setAcceleration(DEFAULT_ACCEL);
  stepper_y.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_y.setAcceleration(DEFAULT_ACCEL);
  stepper_z.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_z.setAcceleration(DEFAULT_ACCEL);
  
  Wire.begin(I2C_ADDR_UNO);
  Wire.onReceive(onI2CReceive);
  
  Serial.begin(9600);
  Serial.println(F("=== Arduino Uno: Advanced Stepper Control ==="));
  Serial.println(F("Sende 't' fuer den TESTMODUS."));
}

void loop() {
  // 1. Endschalter-Überwachung im Normalbetrieb (Software-Limit / Notstopp)
  if (digitalRead(ENDSTOP_X) == LOW && x_homing_state == HOMING_IDLE) {
    if (stepper_x.speed() < 0 || stepper_x.distanceToGo() < 0) {
      stepper_x.stop();
      stepper_x.setCurrentPosition(0); 
      if (test_mode_active) Serial.println(F("NOTSTOPP: Endstopp X im Normalbetrieb ausgeloest!"));
    }
  }

  // 2. Zeitgesteuerte Bewegungen für alle Achsen überwachen (TIMED-Modus)
  for (int i = 0; i < 3; i++) {
    if (timed_move_active[i]) {
      if (millis() - timed_move_start[i] >= timed_move_duration[i]) {
        timed_move_active[i] = false;
        if (i == 0) stepper_x.stop();
        if (i == 1) stepper_y.stop();
        if (i == 2) stepper_z.stop();
        if (test_mode_active) {
          Serial.print(F("TIMED Move Achse ")); Serial.print(i); Serial.println(F(" beendet."));
        }
      }
    }
  }

  // 3. Ablaufsteuerung für das X-Homing
  if (x_homing_state == HOMING_SEARCHING) {
    if (digitalRead(ENDSTOP_X) == LOW) { 
      stepper_x.stop();
      stepper_x.setMaxSpeed(200); 
      stepper_x.move(200); 
      x_homing_state = HOMING_REBOUND;
      if (test_mode_active) Serial.println(F("Endstopp getroffen. Fahre frei..."));
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
      if (test_mode_active) Serial.println(F("X-Kalibrierung ERFOLGREICH! Position auf 0 gesetzt."));
    }
  } 
  else {
    // Normaler Modus für X
    if (timed_move_active[AXIS_X]) stepper_x.runSpeed(); else stepper_x.run();
  }

  // Normaler Modus für Y und Z
  if (timed_move_active[AXIS_Y]) stepper_y.runSpeed(); else stepper_y.run();
  if (timed_move_active[AXIS_Z]) stepper_z.runSpeed(); else stepper_z.run();
  
  handleSerialDebug();
  
  // I2C Befehl verarbeiten (falls nicht im Testmodus)
  if (new_command_received) {
    new_command_received = false;
    if (!test_mode_active) {
      StepperCommand cmd_copy;
      cmd_copy.axis = active_cmd.axis;
      cmd_copy.move_type = active_cmd.move_type;
      cmd_copy.parameter1 = active_cmd.parameter1;
      cmd_copy.parameter2 = active_cmd.parameter2;
      executeStepperCommand(cmd_copy);
    }
  }
  
  // 4. Kontinuierliche Live-Positionsanzeige im Testmodus bei JEDER Bewegung
  static unsigned long last_status = 0;
  if (test_mode_active && (millis() - last_status >= 150)) { // Schnelleres Intervall (150ms) für flüssiges Feedback
    bool is_moving = (stepper_x.distanceToGo() != 0 || stepper_y.distanceToGo() != 0 || stepper_z.distanceToGo() != 0 || 
                      timed_move_active[0] || timed_move_active[1] || timed_move_active[2] || 
                      x_homing_state != HOMING_IDLE);
                      
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
  stepper_x.setSpeed(-400); 
}

void executeStepperCommand(StepperCommand cmd) {
  if (cmd.axis == AXIS_X && x_homing_state == HOMING_IDLE) {
    if (cmd.move_type == MOVE_TYPE_ABSOLUTE && (cmd.parameter1 < 0 || cmd.parameter1 > MAX_STEPS_X)) {
      if (test_mode_active) Serial.println(F("BLOCKIERT: Ziel ausserhalb Software-Limit X (0-8000)!"));
      return;
    }
    if (cmd.move_type == MOVE_TYPE_RELATIVE) {
      long preview = stepper_x.currentPosition() + cmd.parameter1;
      if (preview < 0 || preview > MAX_STEPS_X) {
        if (test_mode_active) Serial.println(F("BLOCKIERT: Relative Fahrt verletzt Software-Limit X!"));
        return;
      }
    }
  }

  AccelStepper* target = nullptr;
  if (cmd.axis == AXIS_X) target = &stepper_x;
  else if (cmd.axis == AXIS_Y) target = &stepper_y;
  else if (cmd.axis == AXIS_Z) target = &stepper_z;
  
  if (target == nullptr) return;
  timed_move_active[cmd.axis] = false; // Vorherigen Timed-Move brechen falls aktiv

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
      else { Serial.println(F("Fehler: Achse unbekannt")); return; }

      if (typeStr == "REL") cmd.move_type = MOVE_TYPE_RELATIVE;
      else if (typeStr == "ABS") cmd.move_type = MOVE_TYPE_ABSOLUTE;
      else if (typeStr == "TIMED") cmd.move_type = MOVE_TYPE_TIMED;
      else { Serial.println(F("Fehler: Typ unbekannt")); return; }

      cmd.parameter1 = p1;
      cmd.parameter2 = p2;

      Serial.print(F("Fuehre aus -> ")); Serial.print(axisStr);
      Serial.print(F(" | Typ: ")); Serial.print(typeStr);
      Serial.print(F(" | P1: ")); Serial.print(p1);
      Serial.print(F(" | P2: ")); Serial.println(p2);

      executeStepperCommand(cmd);
    } else {
      Serial.println(F("Format falsch! Beispiel: STP_X,TIMED,3000,600"));
    }
  }
}

void printHelp() {
  Serial.println(F("Format: [STP_Achse],[Typ],[Param1],[Param2]"));
  Serial.println(F("  STP_X,REL,1000,600      -> Relative Schritte (Schritte, Speed)"));
  Serial.println(F("  STP_Y,ABS,2000,500      -> Absolute Position (Ziel-Schritt, Speed)"));
  Serial.println(F("  STP_Z,TIMED,3000,-400   -> Fahrt auf Zeit (Zeit in ms, konstante Speed)"));
  Serial.println(F("Spezialbefehle:"));
  Serial.println(F("  homeX  -> Startet X-Kalibrierung am Endstopp (Pin 9)"));
  Serial.println(F("  enAll  -> Treiber AN | disAll -> Treiber AUS | t -> Beenden"));
}

void onI2CReceive(int numBytes) {
  if (numBytes > 0 && numBytes >= (int)sizeof(StepperCommand)) {
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
  while (Wire.available()) { Wire.read(); }
}