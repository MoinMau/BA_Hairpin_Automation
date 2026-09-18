#include <Arduino.h>
#include <Wire.h>
#include <AccelStepper.h>
#include "i2c_protocol.h"
#include "config_uno.h"

AccelStepper stepper_x(1, STEPPER_X_STP, STEPPER_X_DIR);
AccelStepper stepper_y(1, STEPPER_Y_STP, STEPPER_Y_DIR);
AccelStepper stepper_z(1, STEPPER_Z_STP, STEPPER_Z_DIR);

// Pin-Arrays für einfachen indexbasierten Zugriff (0=X, 1=Y, 2=Z)
const uint8_t step_pins[3] = {STEPPER_X_STP, STEPPER_Y_STP, STEPPER_Z_STP};
const uint8_t dir_pins[3] = {STEPPER_X_DIR, STEPPER_Y_DIR, STEPPER_Z_DIR};

bool debug_enabled = false;          
volatile bool new_command_received = false;
volatile StepperCommand active_cmd;

// --- Zeitsteuerung (TIMED) ---
unsigned long timed_move_start[3] = {0, 0, 0};
unsigned long timed_move_duration[3] = {0, 0, 0};
bool timed_move_active[3] = {false, false, false};

// --- Vibrationsmodus ---
bool vibrate_active[3] = {false, false, false};
int32_t vibrate_amplitude[3] = {0, 0, 0};     
unsigned long vibrate_half_period[3] = {0, 0, 0}; 
unsigned long vibrate_last_toggle[3] = {0, 0, 0}; 
int32_t vibrate_step_count[3] = {0, 0, 0};
bool vibrate_dir_state[3] = {false, false, false};

// --- 3-Achsen Homing (Zustandsmaschine) ---
enum HomingState { HOMING_IDLE, HOMING_SEARCHING, HOMING_REBOUND };
HomingState axis_homing_state[3] = {HOMING_IDLE, HOMING_IDLE, HOMING_IDLE};

void onI2CReceive(int numBytes);
void onI2CRequest();
void handleSerialCommands();
void executeStepperCommand(StepperCommand cmd);
void triggerHoming(uint8_t axis);
void printUnoHelp();

void setup() {
  pinMode(STEPPER_EN, OUTPUT);
  digitalWrite(STEPPER_EN, LOW); 
  
  for (int i = 0; i < 3; i++) {
    pinMode(ENDSTOP_PINS[i], INPUT_PULLUP);
  }
  
  // Richtungsumkehr pro Achse anwenden (aus config_uno.h: INVERT_X/Y/Z_DIR)
  stepper_x.setPinsInverted(INVERT_X_DIR, false, false);
  stepper_y.setPinsInverted(INVERT_Y_DIR, false, false);
  stepper_z.setPinsInverted(INVERT_Z_DIR, false, false);
  
  stepper_x.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_x.setAcceleration(DEFAULT_ACCEL);
  stepper_y.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_y.setAcceleration(DEFAULT_ACCEL);
  stepper_z.setMaxSpeed(DEFAULT_MAX_SPEED); stepper_z.setAcceleration(DEFAULT_ACCEL);
  
  Wire.begin(I2C_ADDR_UNO);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);
  
  Serial.begin(9600);
  Serial.println(F("=== Arduino Uno: Stepper Slave Controller ==="));
  Serial.println(F("Tippe 'h' fuer die vollstaendige Befehlsuebersicht."));
}

void loop() {
  AccelStepper* steppers[3] = {&stepper_x, &stepper_y, &stepper_z};

  for (int i = 0; i < 3; i++) {
    // 1. Endschalter-Notstopp im Normalbetrieb
    if (digitalRead(ENDSTOP_PINS[i]) == LOW && axis_homing_state[i] == HOMING_IDLE) {
      if (steppers[i]->speed() < 0 || steppers[i]->distanceToGo() < 0) {
        steppers[i]->stop();
        vibrate_active[i] = false;
        steppers[i]->setCurrentPosition(0); 
        if (debug_enabled) { Serial.print(F("[NOTSTOPP] Achse ")); Serial.print(i); Serial.println(F(" ausgeloest!")); }
      }
    }

    // 2. TIMED Modus Überwachung
    if (timed_move_active[i]) {
      if (millis() - timed_move_start[i] >= timed_move_duration[i]) {
        timed_move_active[i] = false;
        steppers[i]->stop();
      }
    }

    // 3. Homing Ablaufsteuerung pro Achse
    if (axis_homing_state[i] == HOMING_SEARCHING) {
      if (digitalRead(ENDSTOP_PINS[i]) == LOW) {
        steppers[i]->stop();
        steppers[i]->setMaxSpeed(abs(HOMING_REBOUND_SPEED[i]));
        steppers[i]->move(HOMING_REBOUND_STEPS[i] * (HOMING_REBOUND_SPEED[i] > 0 ? 1 : -1));
        axis_homing_state[i] = HOMING_REBOUND;
      } else {
        steppers[i]->runSpeed();
      }
    } 
    else if (axis_homing_state[i] == HOMING_REBOUND) {
      steppers[i]->run();
      if (steppers[i]->distanceToGo() == 0) {
        steppers[i]->setCurrentPosition(0);
        steppers[i]->setMaxSpeed(DEFAULT_MAX_SPEED);
        axis_homing_state[i] = HOMING_IDLE;
        if (debug_enabled) { Serial.print(F("[Homing] Achse ")); Serial.print(i); Serial.println(F(" erfolgreich kalibriert.")); }
      }
    } 
    else {
      // Normaler Fahrbetrieb
      if (timed_move_active[i]) steppers[i]->runSpeed(); else steppers[i]->run();
    }
  }

  // 4. Manueller Vibrationsmodus (Direktes Pin-Writing für maximale Performance)
  for (int i = 0; i < 3; i++) {
    if (vibrate_active[i]) {
      if (micros() - vibrate_last_toggle[i] >= vibrate_half_period[i]) {
        vibrate_last_toggle[i] = micros();
        
        // Ein Puls besteht aus HIGH und LOW
        static bool pin_state[3] = {false, false, false};
        pin_state[i] = !pin_state[i];
        digitalWrite(step_pins[i], pin_state[i]);

        // Wenn ein voller Puls (HIGH->LOW) fertig ist, zählen wir den Step
        if (!pin_state[i]) {
          vibrate_step_count[i]++;
          
          // Wenn die Amplitude in eine Richtung erreicht ist -> Richtung umkehren
          if (vibrate_step_count[i] >= vibrate_amplitude[i]) {
            vibrate_step_count[i] = 0;
            vibrate_dir_state[i] = !vibrate_dir_state[i];
            digitalWrite(dir_pins[i], vibrate_dir_state[i]);
          }
        }
      }
    }
  }

  handleSerialCommands();

  // Sicherer Kopiervorgang der I2C-Befehle (Atomarität)
  if (new_command_received) {
    noInterrupts();
    StepperCommand cmd_copy;
    cmd_copy.axis = active_cmd.axis;
    cmd_copy.move_type = active_cmd.move_type;
    cmd_copy.parameter1 = active_cmd.parameter1;
    cmd_copy.parameter2 = active_cmd.parameter2;
    new_command_received = false;
    interrupts();

    executeStepperCommand(cmd_copy);
  }
}

void triggerHoming(uint8_t axis) {
  if (axis > 2) return;
  AccelStepper* steppers[3] = {&stepper_x, &stepper_y, &stepper_z};
  
  if (debug_enabled) { Serial.print(F("[Homing] Starte Suche auf Achse ")); Serial.println(axis); }
  axis_homing_state[axis] = HOMING_SEARCHING;
  timed_move_active[axis] = false;
  vibrate_active[axis] = false;
  steppers[axis]->setSpeed(HOMING_SEARCH_SPEED[axis]); // Konfigurierbare Suchgeschwindigkeit pro Achse
}

void executeStepperCommand(StepperCommand cmd) {
  if (debug_enabled) {
    Serial.print(F("[Execute] Achse: ")); Serial.print(cmd.axis);
    Serial.print(F(" | Typ: ")); Serial.print(cmd.move_type);
    Serial.print(F(" | P1: ")); Serial.print(cmd.parameter1);
    Serial.print(F(" | P2: ")); Serial.println(cmd.parameter2);
  }

  if (cmd.axis > 2) return;

  // Allgemeine Soft-Limit Prüfung (außer bei Vibration/Stop/Homing)
  if (axis_homing_state[cmd.axis] == HOMING_IDLE && 
      (cmd.move_type == MOVE_TYPE_ABSOLUTE || cmd.move_type == MOVE_TYPE_RELATIVE)) {
    
    long targetPos = (cmd.move_type == MOVE_TYPE_ABSOLUTE) ? 
                      cmd.parameter1 : 
                      (cmd.axis == 0 ? stepper_x.currentPosition() : (cmd.axis == 1 ? stepper_y.currentPosition() : stepper_z.currentPosition())) + cmd.parameter1;

    if (targetPos < MIN_POS[cmd.axis] || targetPos > MAX_POS[cmd.axis]) {
      if (debug_enabled) Serial.println(F("[Sicherheit] Zielposition ausserhalb der Soft-Limits!"));
      return;
    }
  }

  AccelStepper* target = nullptr;
  if (cmd.axis == AXIS_X) target = &stepper_x;
  else if (cmd.axis == AXIS_Y) target = &stepper_y;
  else if (cmd.axis == AXIS_Z) target = &stepper_z;
  
  if (target == nullptr) return;
  
  timed_move_active[cmd.axis] = false;
  vibrate_active[cmd.axis] = false;

  switch (cmd.move_type) {
    case MOVE_TYPE_RELATIVE: 
      target->setMaxSpeed(min((float)abs(cmd.parameter2), MAX_SPEED_LIMITS[cmd.axis]));
      target->move(cmd.parameter1);
      break;
      
    case MOVE_TYPE_ABSOLUTE: 
      target->setMaxSpeed(min((float)abs(cmd.parameter2), MAX_SPEED_LIMITS[cmd.axis]));
      target->moveTo(cmd.parameter1);
      break;
      
    case MOVE_TYPE_TIMED:    
      target->setSpeed(cmd.parameter2); // setSpeed erlaubt negative Werte für die Richtung!
      timed_move_duration[cmd.axis] = cmd.parameter1; 
      timed_move_start[cmd.axis] = millis();
      timed_move_active[cmd.axis] = true;
      break;
      
    case MOVE_TYPE_VIBRATE: 
      if (cmd.parameter2 == 0 || cmd.parameter1 == 0) {
        target->stop();
      } else {
        vibrate_amplitude[cmd.axis] = abs(cmd.parameter1);
        // Wir berechnen die Zeit pro HALB-Schritt (für HIGH/LOW Wechsel)
        // Frequenz (Hz) -> Periodendauer (us) -> Geteilt durch 2 (HIGH/LOW)
        vibrate_half_period[cmd.axis] = (1000000UL / (abs(cmd.parameter2) * 2));
        vibrate_last_toggle[cmd.axis] = micros();
        vibrate_step_count[cmd.axis] = 0;
        vibrate_dir_state[cmd.axis] = true;
        
        digitalWrite(dir_pins[cmd.axis], vibrate_dir_state[cmd.axis]);
        vibrate_active[cmd.axis] = true;
      }
      break;
      
    case MOVE_TYPE_STOP: // Aus FREEZE wurde STOP
      target->stop(); 
      target->setCurrentPosition(target->currentPosition()); 
      if (cmd.parameter1 == 99) {
        digitalWrite(STEPPER_EN, HIGH); 
      } else {
        digitalWrite(STEPPER_EN, LOW);  
      }
      break;

    case MOVE_TYPE_HOMING: // NEU: Homing-Zweig
      triggerHoming(cmd.axis);
      break;
  }
}

void onI2CRequest() {
  AccelStepper* steppers[3] = {&stepper_x, &stepper_y, &stepper_z};
  
  StepperStatus status;
  status.current_pos_x = stepper_x.currentPosition();
  status.current_pos_y = stepper_y.currentPosition();
  status.current_pos_z = stepper_z.currentPosition();
  
  // Homing-Flag
  status.homing_active = (axis_homing_state[0] != HOMING_IDLE ||
                          axis_homing_state[1] != HOMING_IDLE ||
                          axis_homing_state[2] != HOMING_IDLE) ? 1 : 0;

  // axis_busy: Bit 0=X, 1=Y, 2=Z
  // Eine Achse gilt als busy, wenn sie keinen Stillstand erreicht hat
  // (distanceToGo != 0 bei position-basierten Fahrten, timed_move oder vibration aktiv)
  status.axis_busy = 0;
  for (int i = 0; i < 3; i++) {
    bool isBusy = false;
    if (axis_homing_state[i] != HOMING_IDLE) {
      isBusy = true;
    } else if (timed_move_active[i]) {
      isBusy = true;
    } else if (vibrate_active[i]) {
      isBusy = true;
    } else if (steppers[i]->distanceToGo() != 0) {
      isBusy = true;
    }
    if (isBusy) status.axis_busy |= (1 << i);
  }

  // Struktur direkt als Byte-Array an den Master schicken
  Wire.write((uint8_t*)&status, sizeof(StepperStatus));
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;
    
    if (input.equalsIgnoreCase("h")) { printUnoHelp(); return; }
    if (input.equalsIgnoreCase("debug")) {
      debug_enabled = !debug_enabled;
      Serial.print(F("-> UNO DEBUG: ")); Serial.println(debug_enabled ? F("AN") : F("AUS"));
      return;
    }
    
    if (input.equalsIgnoreCase("enAll")) {
      StepperCommand cmd = {0, MOVE_TYPE_STOP, 0, 0}; executeStepperCommand(cmd);
      return;
    }
    if (input.equalsIgnoreCase("disAll")) {
      StepperCommand cmd = {0, MOVE_TYPE_STOP, 99, 0}; executeStepperCommand(cmd);
      return;
    }

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

      StepperCommand local_cmd;

      if (axisStr == "STP_X") local_cmd.axis = AXIS_X;
      else if (axisStr == "STP_Y") local_cmd.axis = AXIS_Y;
      else if (axisStr == "STP_Z") local_cmd.axis = AXIS_Z;
      else return;

      if (typeStr == "REL") local_cmd.move_type = MOVE_TYPE_RELATIVE;
      else if (typeStr == "ABS") local_cmd.move_type = MOVE_TYPE_ABSOLUTE;
      else if (typeStr == "TIMED") local_cmd.move_type = MOVE_TYPE_TIMED;
      else if (typeStr == "VIB") local_cmd.move_type = MOVE_TYPE_VIBRATE;
      else if (typeStr == "STOP") local_cmd.move_type = MOVE_TYPE_STOP;
      else if (typeStr == "HOMING") local_cmd.move_type = MOVE_TYPE_HOMING;
      else return;

      local_cmd.parameter1 = p1;
      local_cmd.parameter2 = p2;

      executeStepperCommand(local_cmd);
    }
  }
}

void printUnoHelp() {
  Serial.println(F("\n=================== UNO BEFEHLE ==================="));
  Serial.println(F("Format: [Achse],[Typ],[Param1],[Param2]"));
  Serial.println(F("  STP_X,REL,1000,600    -> Relative Fahrt (Schritte, Speed)"));
  Serial.println(F("  STP_Y,ABS,2000,500    -> Absolute Fahrt (Zielposition, Speed)"));
  Serial.println(F("  STP_Z,TIMED,3000,400  -> Zeitfahrt (Dauer in ms, Speed)"));
  Serial.println(F("  STP_X,VIB,5,50        -> Vibration (Amplitude, Frequenz in Hz)"));
  Serial.println(F("  STP_X,STOP,0,0        -> Stoppt Achse X & friert Position ein"));
  Serial.println(F("  STP_Y,HOMING,0,0      -> Startet Homing-Fahrt fuer Achse Y"));
  Serial.println(F("Systembefehle:"));
  Serial.println(F("  debug  -> Schaltet Konsolenmeldungen AN/AUS"));
  Serial.println(F("  enAll  -> Treiber AN  |  disAll -> Treiber AUS"));
  Serial.println(F("==================================================="));
}

void onI2CReceive(int numBytes) {
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
  while (Wire.available()) { Wire.read(); }
}