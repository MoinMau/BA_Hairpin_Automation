#include <Arduino.h>
#include <Wire.h>
#include <AccelStepper.h>
#include "i2c_protocol.h"

// CNC-Shield V3.0 Pin-Belegung
#define STEPPER_X_STP 2   //
#define STEPPER_X_DIR 5   //
#define STEPPER_Y_STP 3   //
#define STEPPER_Y_DIR 6   //
#define STEPPER_Z_STP 4   //
#define STEPPER_Z_DIR 7   //
#define STEPPER_EN 8      // Gemeinsamer Hardware-Enable Pin für ALLE Treiber

AccelStepper stepper_x(1, STEPPER_X_STP, STEPPER_X_DIR);
AccelStepper stepper_y(1, STEPPER_Y_STP, STEPPER_Y_DIR);
AccelStepper stepper_z(1, STEPPER_Z_STP, STEPPER_Z_DIR);

#define DEFAULT_MAX_SPEED 1000.0      
#define DEFAULT_ACCEL 500.0    

// Volatile Variablen für I2C-Datenübergabe
volatile bool new_command_received = false;
volatile StepperCommand active_cmd;

// Zeitsteuerung für "Move mit Speed und Zeit"
unsigned long timed_move_start[3] = {0, 0, 0};
unsigned long timed_move_duration[3] = {0, 0, 0};
bool timed_move_active[3] = {false, false, false};

bool test_mode_active = false; 

void onI2CReceive(int numBytes);
void handleSerialDebug();
void executeStepperCommand(StepperCommand cmd);
void printHelp();

void setup() {
  // Fehlerbehebung: Alle Treiber über gemeinsamen Pin 8 aktivieren (LOW = an)
  pinMode(STEPPER_EN, OUTPUT);
  digitalWrite(STEPPER_EN, LOW); 
  
  stepper_x.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_x.setAcceleration(DEFAULT_ACCEL);
  stepper_y.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_y.setAcceleration(DEFAULT_ACCEL);
  stepper_z.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_z.setAcceleration(DEFAULT_ACCEL);
  
  Wire.begin(I2C_ADDR_UNO);
  Wire.onReceive(onI2CReceive);
  
  Serial.begin(9600);
  Serial.println(F("=== Arduino Uno: Advanced Stepper Control ==="));
  Serial.println(F("Sende 't' für den TESTMODUS."));
}

void loop() {
  // 1. Überwachung der zeitgesteuerten Bewegungen (MOVE_TYPE_TIMED)
  for (int i = 0; i < 3; i++) {
    if (timed_move_active[i]) {
      if (millis() - timed_move_start[i] >= timed_move_duration[i]) {
        timed_move_active[i] = false;
        if (i == 0) stepper_x.stop();
        if (i == 1) stepper_y.stop();
        if (i == 2) stepper_z.stop();
        if (test_mode_active) {
          Serial.print(F("Zeitgesteuerte Bewegung Achse ")); Serial.print(i); Serial.println(F(" BEENDET."));
        }
      }
    }
  }

  // 2. Kontinuierliches Puls-Update für die Motoren
  // Wenn zeitgesteuert, nutzen wir runSpeed() [konstante Fahrt], sonst das normale run() [mit Rampe]
  if (timed_move_active[AXIS_X]) stepper_x.runSpeed(); else stepper_x.run();
  if (timed_move_active[AXIS_Y]) stepper_y.runSpeed(); else stepper_y.run();
  if (timed_move_active[AXIS_Z]) stepper_z.runSpeed(); else stepper_z.run();
  
  handleSerialDebug();
  
  // 3. I2C Befehl verarbeiten (falls Testmodus inaktiv)
  if (new_command_received) {
    new_command_received = false;
    
    if (!test_mode_active) {
      // Lokale, nicht-volatile Kopie erstellen
      StepperCommand cmd_copy;
      cmd_copy.axis = active_cmd.axis;
      cmd_copy.move_type = active_cmd.move_type;
      cmd_copy.parameter1 = active_cmd.parameter1;
      cmd_copy.parameter2 = active_cmd.parameter2;
      
      // Sichere Kopie an die Funktion übergeben
      executeStepperCommand(cmd_copy);
    } else {
      Serial.println(F("I2C-Befehl blockiert: Testmodus aktiv!"));
    }
  }
  
  // Positionsausgabe im Testmodus
  static unsigned long last_status = 0;
  if (test_mode_active && (millis() - last_status >= 500)) {
    if (stepper_x.distanceToGo() != 0 || stepper_y.distanceToGo() != 0 || stepper_z.distanceToGo() != 0 || timed_move_active[0] || timed_move_active[1] || timed_move_active[2]) {
      Serial.print(F("Pos -> X:")); Serial.print(stepper_x.currentPosition());
      Serial.print(F(" Y:")); Serial.print(stepper_y.currentPosition());
      Serial.print(F(" Z:")); Serial.println(stepper_z.currentPosition());
    }
    last_status = millis();
  }
}

// Zentrale Funktion zur Befehlsausführung (I2C & Seriell nutzen diese)
void executeStepperCommand(StepperCommand cmd) {
  AccelStepper* target = nullptr;
  if (cmd.axis == AXIS_X) target = &stepper_x;
  else if (cmd.axis == AXIS_Y) target = &stepper_y;
  else if (cmd.axis == AXIS_Z) target = &stepper_z;
  
  if (target == nullptr) return;

  // Falls ein neuer Befehl kommt, brechen wir eine eventuell laufende Zeit-Bewegung ab
  timed_move_active[cmd.axis] = false;

  switch (cmd.move_type) {
    case MOVE_TYPE_RELATIVE: // 1. Direkt Schritte angeben (Vorzeichen = Richtung)
      target->setMaxSpeed(cmd.parameter2);
      target->move(cmd.parameter1);
      break;
      
    case MOVE_TYPE_ABSOLUTE: // 2. Move to position
      target->setMaxSpeed(cmd.parameter2);
      target->moveTo(cmd.parameter1);
      break;
      
    case MOVE_TYPE_TIMED:    // 3. Move mit Speed und Zeit (Dauer)
      target->setSpeed(cmd.parameter2); // parameter2 hält den Speed (z.B. -800 für CCW, 800 für CW)
      timed_move_duration[cmd.axis] = cmd.parameter1; // parameter1 hält die Millisekunden
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
    
    if (input.equalsIgnoreCase("h")) { printHelp(); return; }
    
    // Globale Aktivierung / Deaktivierung aller Treiber (Fehlerbehebung Shield V3)
    if (input.equalsIgnoreCase("enAll")) {
      digitalWrite(STEPPER_EN, LOW); // LOW = Alle Treiber an
      Serial.println(F("ALLE Schrittmotortreiber eingeschaltet (Bestromt)."));
      return;
    }
    if (input.equalsIgnoreCase("disAll")) {
      digitalWrite(STEPPER_EN, HIGH); // HIGH = Alle Treiber stromlos
      Serial.println(F("ALLE Schrittmotortreiber ausgeschaltet (Stromlos)."));
      return;
    }

    // Parsen des strukturierten Befehls: "X,1,1000,500"
    // Format: [Achse],[MoveType],[Param1],[Param2]
    int firstComma = input.indexOf(',');
    int secondComma = input.indexOf(',', firstComma + 1);
    int thirdComma = input.indexOf(',', secondComma + 1);

    if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
      char axisChar = input.charAt(0);
      uint8_t type = input.substring(firstComma + 1, secondComma).toInt();
      int32_t p1 = input.substring(secondComma + 1, thirdComma).toInt();
      int16_t p2 = input.substring(thirdComma + 1).toInt();

      StepperCommand cmd;
      if (axisChar == 'x' || axisChar == 'X') cmd.axis = AXIS_X;
      else if (axisChar == 'y' || axisChar == 'Y') cmd.axis = AXIS_Y;
      else if (axisChar == 'z' || axisChar == 'Z') cmd.axis = AXIS_Z;
      else { Serial.println(F("Ungültige Achse!")); return; }

      cmd.move_type = type;
      cmd.parameter1 = p1;
      cmd.parameter2 = p2;

      Serial.print(F("Führe seriellen Befehl aus: Achse=")); Serial.print(axisChar);
      Serial.print(F(", Typ=")); Serial.print(type);
      Serial.print(F(", P1=")); Serial.print(p1);
      Serial.print(F(", P2=")); Serial.println(p2);

      executeStepperCommand(cmd);
    } else {
      Serial.println(F("Falsches Befehlsformat! Siehe Hilfe (h)."));
    }
  }
}

void printHelp() {
  Serial.println(F("Befehlsstruktur: [Achse],[Move-Type],[Parameter1],[Parameter2]"));
  Serial.println(F("  Typ 1 (Direkt Schritte) -> X,1,[Schritte],[Speed]        (z.B. X,1,1000,600 oder X,1,-500,600)"));
  Serial.println(F("  Typ 2 (Move to Position) -> X,2,[Ziel-Position],[Speed]  (z.B. X,2,2000,500)"));
  Serial.println(F("  Typ 3 (Zeitgesteuert)    -> X,3,[Zeit in ms],[Speed]     (z.B. X,3,3000,800 für 3 Sek Fahrt)"));
  Serial.println(F("Globale Shield-Treiber-Steuerung:"));
  Serial.println(F("  enAll  -> Aktiviert ALLE Treiber (Motoren unter Haltestrom)"));
  Serial.println(F("  disAll -> Deaktiviert ALLE Treiber (Motoren komplett stromlos und frei drehbar)"));
  Serial.println(F("  t      -> Testmodus BEENDEN"));
}

// I2C Interrupt Handler
void onI2CReceive(int numBytes) {
  if (numBytes >= (int)sizeof(StepperCommand)) {
    byte buffer[sizeof(StepperCommand)];
    for (int i = 0; i < (int)sizeof(StepperCommand); i++) {
      buffer[i] = Wire.read();
    }
    
    // Cast in einen temporären, normalen Zeiger
    StepperCommand* cmd_ptr = (StepperCommand*)buffer;
    
    // Explizit Feld für Feld in die volatile Struktur übertragen
    active_cmd.axis = cmd_ptr->axis;
    active_cmd.move_type = cmd_ptr->move_type;
    active_cmd.parameter1 = cmd_ptr->parameter1;
    active_cmd.parameter2 = cmd_ptr->parameter2;
    
    new_command_received = true; 
  }
  while (Wire.available()) { Wire.read(); }
}