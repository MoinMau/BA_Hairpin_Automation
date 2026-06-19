#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define LED_PIN 2  

bool test_mode_active = false;

// ============================================================================
// NON-BLOCKING PROGRAM ENGINE
// ============================================================================
// Definiere deine Sequenz als Schritt-für-Schritt Programm in setup().
// Das Programm läuft dann non-blocking in loop() — kein delay(), kein Hänger.
// Der serielle Testmodus (`t` drücken) und `stop` funktionieren parallel.
//
// Benutzung:
//   void setup() {
//     ...
//     prog_home(AXIS_X);
//     prog_wait_axis(AXIS_X);
//     prog_abs(AXIS_X, 5000, 800);
//     prog_wait_axis(AXIS_X);
//     prog_servo(0, 490);
//     prog_wait_ms(2000);
//     prog_vibrate(AXIS_Z, 1, 40);
//     prog_wait_ms(1000);
//     prog_stop(AXIS_Z);
//     prog_end();
//     prog_start();  // Start sofort bei Boot
//   }
//
// Über Serial: `run` startet das Programm, `stop` bricht ab.
// ============================================================================

// --- Schritt-Typen für das Programm ---
enum ProgramCmd : uint8_t {
  CMD_ABS,          // Absolute Fahrt:   axis, position,    speed
  CMD_REL,          // Relative Fahrt:   axis, steps,       speed
  CMD_TIMED,        // Zeit-Fahrt:       axis, duration_ms, speed
  CMD_VIB,          // Vibration:        axis, amplitude,   freq_hz
  CMD_STOP,         // Achse stoppen:    axis, -,           -
  CMD_HOME,         // Homing:           axis, -,           -
  CMD_ENABLE,       // Treiber an
  CMD_DISABLE,      // Treiber aus
  CMD_SERVO,        // Servo setzen:     servo_num, value,  -
  CMD_WAIT_MS,      // Warten (ms):      -, ms,             -
  CMD_WAIT_AXIS,    // Warten auf Achse: axis, -,           -
  CMD_WAIT_ALL,     // Warten auf alle Achsen
  CMD_STATUS,       // Status ausgeben
  CMD_END,          // Programm-Ende
};

struct ProgramStep {
  ProgramCmd cmd;
  uint8_t axis;
  int32_t param1;
  int16_t param2;
};

#define MAX_PROGRAM_STEPS 64
static ProgramStep program_buffer[MAX_PROGRAM_STEPS];
static int program_count = 0;
static int program_index = -1;   // -1 = idle
static unsigned long program_wait_start = 0;
static unsigned long program_poll_last = 0;
static const unsigned long POLL_INTERVAL_MS = 50;  // I2C-Polling im Wait-Zustand

// --- Programm-Builder (rufst du in setup() auf) ---

void prog_clear() { program_count = 0; }

void prog_abs(uint8_t axis, int32_t position, int16_t speed) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_ABS, axis, position, speed};
}

void prog_rel(uint8_t axis, int32_t steps, int16_t speed) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_REL, axis, steps, speed};
}

void prog_timed(uint8_t axis, unsigned long duration_ms, int16_t speed) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_TIMED, axis, (int32_t)duration_ms, speed};
}

void prog_vibrate(uint8_t axis, int32_t amplitude, int16_t freq_hz) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_VIB, axis, amplitude, freq_hz};
}

void prog_stop(uint8_t axis) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_STOP, axis, 0, 0};
}

void prog_home(uint8_t axis) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_HOME, axis, 0, 0};
}

void prog_enable() {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_ENABLE, 0, 0, 0};
}

void prog_disable() {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_DISABLE, 0, 0, 0};
}

void prog_servo(uint8_t num, uint16_t value) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_SERVO, num, value, 0};
}

void prog_wait_ms(unsigned long ms) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_WAIT_MS, 0, (int32_t)ms, 0};
}

void prog_wait_axis(uint8_t axis) {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_WAIT_AXIS, axis, 0, 0};
}

void prog_wait_all() {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_WAIT_ALL, 0, 0, 0};
}

void prog_status() {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_STATUS, 0, 0, 0};
}

void prog_end() {
  if (program_count >= MAX_PROGRAM_STEPS) return;
  program_buffer[program_count++] = {CMD_END, 0, 0, 0};
}

// --- Programm-Steuerung ---

void prog_start() {
  if (program_count == 0) {
    Serial.println(F("[PROG] Keine Schritte definiert!"));
    return;
  }
  program_index = 0;
  program_wait_start = millis();
  Serial.println(F("[PROG] Programm gestartet."));
}

void prog_stop_exec() {
  if (program_index >= 0) {
    Serial.println(F("[PROG] Programm abgebrochen."));
    program_index = -1;
    // Alle Achsen stoppen als Sicherheit
    sendStepperCommand({AXIS_X, MOVE_TYPE_STOP, 0, 0});
  }
}

bool prog_is_running() {
  return program_index >= 0;
}

// --- Programmschritt ausführen (ein Schritt pro Aufruf) ---

static void prog_execute_step(const ProgramStep& step) {
  switch (step.cmd) {
    case CMD_ABS: {
      StepperCommand c = {step.axis, MOVE_TYPE_ABSOLUTE, step.param1, step.param2};
      sendStepperCommand(c);
      break;
    }
    case CMD_REL: {
      StepperCommand c = {step.axis, MOVE_TYPE_RELATIVE, step.param1, step.param2};
      sendStepperCommand(c);
      break;
    }
    case CMD_TIMED: {
      StepperCommand c = {step.axis, MOVE_TYPE_TIMED, step.param1, step.param2};
      sendStepperCommand(c);
      break;
    }
    case CMD_VIB: {
      StepperCommand c = {step.axis, MOVE_TYPE_VIBRATE, step.param1, step.param2};
      sendStepperCommand(c);
      break;
    }
    case CMD_STOP: {
      StepperCommand c = {step.axis, MOVE_TYPE_STOP, 0, 0};
      sendStepperCommand(c);
      break;
    }
    case CMD_HOME: {
      StepperCommand c = {step.axis, MOVE_TYPE_HOMING, 0, 0};
      sendStepperCommand(c);
      break;
    }
    case CMD_ENABLE: {
      StepperCommand c = {0, MOVE_TYPE_STOP, 0, 0};
      sendStepperCommand(c);
      break;
    }
    case CMD_DISABLE: {
      StepperCommand c = {0, MOVE_TYPE_STOP, 99, 0};
      sendStepperCommand(c);
      break;
    }
    case CMD_SERVO:
      sendServoCommand(step.axis, (uint16_t)step.param1);
      break;
    case CMD_WAIT_MS:
      program_wait_start = millis();
      break;
    case CMD_WAIT_AXIS:
    case CMD_WAIT_ALL:
      program_wait_start = millis();
      break;
    case CMD_STATUS:
      print_status();
      break;
    case CMD_END:
      program_index = -1;
      Serial.println(F("[PROG] Programm beendet."));
      return;  // kein ++ von index
    default:
      break;
  }
  program_index++;
}

// --- Bereitschafts-Prüfung für Wait-Schritte ---

static bool prog_wait_done(const ProgramStep& step) {
  // Polling-Limit: nicht bei jedem loop()-Durchlauf I2C abfragen
  if (millis() - program_poll_last < POLL_INTERVAL_MS) {
    return false;  // Noch nicht prüfen
  }
  program_poll_last = millis();

  if (step.cmd == CMD_WAIT_MS) {
    return (millis() - program_wait_start) >= (unsigned long)step.param1;
  }

  // Axis/All Busy abfragen
  uint8_t received = Wire.requestFrom((uint8_t)I2C_ADDR_UNO, (uint8_t)sizeof(StepperStatus));
  if (received < sizeof(StepperStatus)) return false;

  StepperStatus st;
  Wire.readBytes((uint8_t*)&st, sizeof(StepperStatus));

  if (step.cmd == CMD_WAIT_AXIS) {
    return (st.axis_busy & (1 << step.axis)) == 0;
  }
  if (step.cmd == CMD_WAIT_ALL) {
    return st.axis_busy == 0;
  }
  return true;
}

// --- Programm-Runner (aus loop() aufrufen) ---

void prog_update() {
  if (program_index < 0 || program_index >= program_count) return;

  ProgramStep& current = program_buffer[program_index];

  // Bei Wait-Schritten: prüfen, ob Bedingung erfüllt
  if (current.cmd == CMD_WAIT_MS || current.cmd == CMD_WAIT_AXIS || current.cmd == CMD_WAIT_ALL) {
    if (prog_wait_done(current)) {
      program_index++;  // Bedingung erfüllt → weiter
    }
    return;  // Noch warten
  }

  // Normale Kommando-Schritte: sofort ausführen
  prog_execute_step(current);
}

// ============================================================================
// DIREKTE I2C-KOMMANDOS (für Testmodus per Serial)
// ============================================================================

void sendStepperCommand(StepperCommand cmd) {
  digitalWrite(LED_PIN, HIGH);
  Wire.beginTransmission(I2C_ADDR_UNO);
  Wire.write((uint8_t*)&cmd, sizeof(StepperCommand));
  uint8_t error = Wire.endTransmission();
  digitalWrite(LED_PIN, LOW);
  
  if (error != 0) {
    Serial.print(F("[I2C FEHLER] Code: ")); Serial.println(error);
  } else {
    Serial.println(F("[I2C SUCCESS] Befehl an Uno übertragen."));
  }
}

void sendServoCommand(uint8_t num, uint16_t val) {
  digitalWrite(LED_PIN, HIGH);
  ServoCommand cmd = {num, val};
  Wire.beginTransmission(I2C_ADDR_NANO);
  Wire.write((uint8_t*)&cmd, sizeof(ServoCommand));
  uint8_t error = Wire.endTransmission();
  digitalWrite(LED_PIN, LOW);
  
  if (error != 0) {
    Serial.print(F("[I2C SERVO FEHLER] Code: ")); Serial.println(error);
  }
}

// ============================================================================
// HILFS-FUNKTIONEN
// ============================================================================

StepperStatus get_stepper_status() {
  StepperStatus status = {0, 0, 0, 0, 0};
  uint8_t received = Wire.requestFrom((uint8_t)I2C_ADDR_UNO, (uint8_t)sizeof(StepperStatus));
  if (received >= sizeof(StepperStatus)) {
    Wire.readBytes((uint8_t*)&status, sizeof(StepperStatus));
  }
  return status;
}

bool is_axis_busy(uint8_t axis) {
  return (get_stepper_status().axis_busy & (1 << axis)) != 0;
}

void scanI2CBus() {
  Serial.println(F("\n--- Starte I2C-Bus-Scan... ---"));
  byte error, address;
  int nDevices = 0;
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("Gerat gefunden auf Adresse 0x"));
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      if (address == I2C_ADDR_UNO) Serial.println(F(" <- [KORREKT] Arduino Uno Stepper-Board"));
      else if (address == I2C_ADDR_NANO) Serial.println(F(" <- [KORREKT] Arduino Nano Servo-Board"));
      else Serial.println();
      nDevices++;
    }
  }
  if (nDevices == 0) Serial.println(F("WARNUNG: Keine I2C-Geraete gefunden!\n"));
}

void print_status() {
  Serial.println(F("\n================= SYSTEM STATUS REPORT ================="));
  
  // 1. Uno abfragen (Schrittmotoren)
  uint8_t bytesReceived = Wire.requestFrom((uint8_t)I2C_ADDR_UNO, (uint8_t)sizeof(StepperStatus));
  if (bytesReceived >= sizeof(StepperStatus)) {
    StepperStatus uStatus;
    Wire.readBytes((uint8_t*)&uStatus, sizeof(StepperStatus));
    Serial.println(F("[SLAVE 1: ARDUINO UNO (0x33)]"));
    Serial.print(F("  -> Position X : ")); Serial.print(uStatus.current_pos_x); Serial.println(F(" Steps"));
    Serial.print(F("  -> Position Y : ")); Serial.print(uStatus.current_pos_y); Serial.println(F(" Steps"));
    Serial.print(F("  -> Position Z : ")); Serial.print(uStatus.current_pos_z); Serial.println(F(" Steps"));
    Serial.print(F("  -> Homing     : ")); Serial.println(uStatus.homing_active ? F("LAEUFT") : F("IDLE"));
    Serial.print(F("  -> Busy       : "));
    Serial.print(uStatus.axis_busy & BUSY_X ? F("X") : F("."));
    Serial.print(uStatus.axis_busy & BUSY_Y ? F("Y") : F("."));
    Serial.println(uStatus.axis_busy & BUSY_Z ? F("Z") : F("."));
  }
  
  Serial.println(F("-------------------------------------------------------"));

  // 2. Nano ABFRAGE TYP 1: Servo-Stellungen (0-1000)
  Wire.beginTransmission(I2C_ADDR_NANO);
  Wire.write(REQ_NANO_SERVOS);
  Wire.endTransmission();
  Wire.requestFrom(I2C_ADDR_NANO, sizeof(ServoStatus));
  if (Wire.available() >= (int)sizeof(ServoStatus)) {
    ServoStatus nStatus;
    Wire.readBytes((uint8_t*)&nStatus, sizeof(ServoStatus));
    Serial.println(F("[SLAVE 2: ARDUINO NANO (0x32) - SERVOS]"));
    Serial.print(F("  -> Stellwerte (0-1000): "));
    for(int i = 0; i < 6; i++) {
      Serial.print(F("S")); Serial.print(i); Serial.print(F(":")); 
      Serial.print(nStatus.current_val[i]); Serial.print(F("  "));
    }
    Serial.println();
  }

  Serial.println(F("-------------------------------------------------------"));

  // 3. Nano ABFRAGE TYP 2: Sensor-Telemetrie & Spannungen
  Wire.beginTransmission(I2C_ADDR_NANO);
  Wire.write(REQ_NANO_SENSORS);
  Wire.endTransmission();
  Wire.requestFrom(I2C_ADDR_NANO, sizeof(SensorStatus));
  if (Wire.available() >= (int)sizeof(SensorStatus)) {
    SensorStatus snStatus;
    Wire.readBytes((uint8_t*)&snStatus, sizeof(SensorStatus));
    
    float volt_a1 = (snStatus.analog_a1 * 5.0) / 1023.0;
    float volt_a2 = (snStatus.analog_a2 * 5.0) / 1023.0;
    float volt_a3 = (snStatus.analog_a3 * 5.0) / 1023.0;
    float volt_mh = (snStatus.mh_a7_raw * 5.0) / 1023.0;

    Serial.println(F("[SLAVE 2: ARDUINO NANO (0x32) - SENSOREN & SPANNUNGEN]"));
    Serial.print(F("  -> Shunt Strommessung (A0 Raw) : ")); Serial.println(snStatus.shunt_raw);
    Serial.print(F("  -> MH-Sensor Digital (D2)      : ")); Serial.println(snStatus.mh_d2_state == HIGH ? F("HIGH") : F("LOW"));
    Serial.print(F("  -> MH-Sensor Analog (A7)       : ")); Serial.print(volt_mh); Serial.println(F(" V"));
    Serial.print(F("  -> Spannung Pin A1             : ")); Serial.print(volt_a1); Serial.println(F(" V"));
    Serial.print(F("  -> Spannung Pin A2             : ")); Serial.print(volt_a2); Serial.println(F(" V"));
    Serial.print(F("  -> Spannung Pin A3             : ")); Serial.print(volt_a3); Serial.println(F(" V"));
  }
  Serial.println(F("========================================================\n"));
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT); 
  digitalWrite(LED_PIN, LOW);
  Wire.begin(I2C_SDA, I2C_SCL, 100000); 
  
  Serial.println(F("\n=================================================="));
  Serial.println(F("       ESP32: ADVANCED I2C MASTER CENTER          "));
  Serial.println(F("=================================================="));
  Serial.println(F(" -> Druecke 't' + Enter, um das System zu starten."));
  Serial.println(F(" -> Tippe 'scan' ein, um die Hardware zu pruefen."));
  Serial.println(F(" -> 'run' startet das Programm, 'stop' bricht ab."));
  Serial.println(F("--------------------------------------------------"));

  // ================================================================
  // 👇 HIER DEIN MINI-PROGRAMM DEFINIEREN
  //
  // Beispiel 1: Homing → Position → Servo → Vibration
  //   prog_home(AXIS_X);
  //   prog_wait_axis(AXIS_X);
  //   prog_abs(AXIS_X, 5000, 800);
  //   prog_wait_axis(AXIS_X);
  //   prog_servo(0, 490);
  //   prog_wait_ms(2000);
  //   prog_vibrate(AXIS_Z, 1, 40);
  //   prog_wait_ms(1000);
  //   prog_stop(AXIS_Z);
  //   prog_end();
  //   prog_start();  // ← Start bei Boot
  //
  // Beispiel 2: Nur per 'run' Befehl starten (kein prog_start()):
  //   (Programm ist definiert, startet aber nicht automatisch)
  // ================================================================

  // prog_ ... hier einfügen
  // prog_end();
  // prog_start();  // Auskommentieren = nur per 'run' startbar
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  handleSerialMaster();   // Serielle Befehle (auch während Programm)
  prog_update();          // Non-blocking Programm-Abarbeitung
}

// ============================================================================
// SERIELLE SCHNITTSTELLE (Testmodus + Programmsteuerung)
// ============================================================================

void handleSerialMaster() {
  if (Serial.available() > 0) {
    char ch = Serial.peek();
    
    if (ch == 't' || ch == 'T') {
      Serial.read(); 
      test_mode_active = !test_mode_active;
      Serial.println(F("\n=================================================="));
      Serial.print(F(" MASTER-KONTROLLE: ")); Serial.println(test_mode_active ? F("ONLINE") : F("STANDBY"));
      Serial.println(F("=================================================="));
      if (test_mode_active) printMasterHelp();
      return;
    }
    
    if (!test_mode_active) {
      String checkScan = Serial.readStringUntil('\n');
      checkScan.trim();
      if (checkScan.equalsIgnoreCase("scan")) scanI2CBus();
      else while(Serial.available()) Serial.read();
      return;
    }
    
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;
    
    if (input.equalsIgnoreCase("h")) { printMasterHelp(); return; }
    if (input.equalsIgnoreCase("scan")) { scanI2CBus(); return; } 
    if (input.equalsIgnoreCase("status")) { print_status(); return; }
    if (input.equalsIgnoreCase("run")) {
      if (prog_is_running()) {
        Serial.println(F("[WARNUNG] Programm läuft bereits!"));
      } else if (program_count == 0) {
        Serial.println(F("[WARNUNG] Kein Programm definiert!"));
      } else {
        prog_start();
      }
      return;
    }
    
    if (input.equalsIgnoreCase("stop")) {
      prog_stop_exec();
      return;
    }

    if (input.equalsIgnoreCase("enAll") || input.equalsIgnoreCase("disAll")) {
      bool isDisable = input.equalsIgnoreCase("disAll");
      StepperCommand cmd = {0, MOVE_TYPE_STOP, (int32_t)(isDisable ? 99 : 0), 0};
      
      digitalWrite(LED_PIN, HIGH);
      Wire.beginTransmission(I2C_ADDR_UNO);
      Wire.write((uint8_t*)&cmd, sizeof(StepperCommand));
      uint8_t err = Wire.endTransmission();
      digitalWrite(LED_PIN, LOW);
      
      if (err == 0) {
        Serial.println(F("\n[I2C SEND SUCCESS]"));
        Serial.print(F("  -> Ziel-Controller : ARDUINO UNO (0x33)\n  -> System-Aktion   : "));
        Serial.println(isDisable ? F("Treiber DEAKTIVIERT (Stromlos)") : F("Treiber AKTIVIERT (Unter Haltestrom)"));
      }
      return;
    }

    if (input.startsWith("STP_") || input.startsWith("stp_")) {
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

        if (typeStr == "REL") cmd.move_type = MOVE_TYPE_RELATIVE;
        else if (typeStr == "ABS") cmd.move_type = MOVE_TYPE_ABSOLUTE;
        else if (typeStr == "TIMED") cmd.move_type = MOVE_TYPE_TIMED;
        else if (typeStr == "VIB") cmd.move_type = MOVE_TYPE_VIBRATE;
        else if (typeStr == "STOP") cmd.move_type = MOVE_TYPE_STOP;
        else if (typeStr == "HOMING") cmd.move_type = MOVE_TYPE_HOMING;

        cmd.parameter1 = p1; cmd.parameter2 = p2;

        digitalWrite(LED_PIN, HIGH);
        Wire.beginTransmission(I2C_ADDR_UNO);
        Wire.write((uint8_t*)&cmd, sizeof(StepperCommand));
        uint8_t err = Wire.endTransmission();
        digitalWrite(LED_PIN, LOW);
        
        if (err == 0) {
          Serial.println(F("\n[I2C SEND SUCCESS]"));
          Serial.println(F("  -> Ziel-Gerät: Arduino Uno (0x33) | Subsystem: Schrittmotoren"));
          Serial.print(F("  -> Parameter : Achse=")); Serial.print(axisStr);
          Serial.print(F(" | Modus=")); Serial.print(typeStr);
          Serial.print(F(" | P1=")); Serial.print(p1);
          Serial.print(F(" | P2=")); Serial.println(p2);
        } else {
          Serial.print(F("\n[I2C TRANSMISSION FAILED] Bus-Fehlercode: ")); Serial.println(err);
        }
      }
    }
    else if (input.startsWith("SRV_") || input.startsWith("srv_")) {
      int firstComma = input.indexOf(',');
      if (firstComma != -1) {
        uint8_t s_num = input.substring(4, firstComma).toInt(); 
        uint16_t pwm = input.substring(firstComma + 1).toInt();

        ServoCommand cmd = {s_num, pwm};

        digitalWrite(LED_PIN, HIGH);
        Wire.beginTransmission(I2C_ADDR_NANO);
        Wire.write((uint8_t*)&cmd, sizeof(ServoCommand));
        uint8_t err = Wire.endTransmission();
        digitalWrite(LED_PIN, LOW);
        
        if (err == 0) {
          Serial.println(F("\n[I2C SEND SUCCESS]"));
          Serial.println(F("  -> Ziel-Gerät: Arduino Nano (0x32) | Subsystem: Servo-Ansteuerung"));
          Serial.print(F("  -> Parameter : Servo-Index=")); Serial.print(s_num);
          Serial.print(F(" (Pin D")); Serial.print(7 + s_num); 
          Serial.print(F(") | gesetzter PWM-Wert=")); Serial.print(pwm); Serial.println(F(" µs"));
        } else {
          Serial.print(F("\n[I2C TRANSMISSION FAILED] Bus-Fehlercode: ")); Serial.println(err);
        }
      }
    }
  }
}

void printMasterHelp() {
  Serial.println(F("\n=================== ESP32 MULTI-SLAVE MATRIX ==================="));
  Serial.println(F(" [SLAVE 1: ARDUINO UNO (0x33)] - SCHRITTMOTOREN"));
  Serial.println(F("  Format: STP_[Achse],[Modus],[Param1],[Param2]"));
  Serial.println(F("  STP_X,REL,2000,800     -> Relative Fahrt (2000 Steps, 800 Steps/s)"));
  Serial.println(F("  STP_Y,ABS,4000,600     -> Absolute Koordinate (Ziel 4000, 600 Steps/s)"));
  Serial.println(F("  STP_Z,TIMED,5000,-300  -> Zeitfahrt (5000 ms, -300 Steps/s)"));
  Serial.println(F("  STP_X,VIB,6,45         -> Vibration (6 Steps Amplitude, 45 Hz)"));
  Serial.println(F("  STP_X,STOP,0,0         -> Stoppt Achse & haelt Position"));
  Serial.println(F("  STP_X,HOMING,0,0       -> Startet Endschalter-Kalibrierung Achse X"));
  Serial.println(F(" -----------------------------------------------------------------"));
  Serial.println(F(" [SLAVE 2: ARDUINO NANO (0x32)] - SERVO-ANSTEUERUNG"));
  Serial.println(F("  Format: SRV_[ServoNummer],[PWM-Wert]"));
  Serial.println(F("  SRV_0,1500             -> Setzt Servo 0 (Pin D7) in Mittelstellung"));
  Serial.println(F("  SRV_5,2200             -> Setzt Servo 5 (Pin D12) auf Pulsweite 2200us"));
  Serial.println(F(" -----------------------------------------------------------------"));
  Serial.println(F(" PROGRAMM-STEUERUNG:"));
  Serial.println(F("  run     -> Startet das definierte Programm"));
  Serial.println(F("  stop    -> Bricht das laufende Programm ab"));
  Serial.println(F(" -----------------------------------------------------------------"));
  Serial.println(F(" Telemetrie- & Direktbefehle:"));
  Serial.println(F("  status  -> Fordert Live-Meldungen (Positionen/PWM) von beiden Slaves an"));
  Serial.println(F("  enAll   -> Alle Motortreiber EIN"));
  Serial.println(F("  disAll  -> Alle Motortreiber AUS (Motoren stromlos)"));
  Serial.println(F("  scan    -> Scannt den I2C-Bus nach beiden Slaves ab"));
  Serial.println(F("  h       -> Zeigt diese Befehlsmatrix an"));
  Serial.println(F("  t       -> Beendet den Master-Testmodus"));
  Serial.println(F("==================================================================\n"));
}

/*
=== PROGRAMM-ENGINE REFERENZ ===

Du definierst dein Programm in setup() mit Builder-Funktionen.
Das Programm läuft dann non-blocking in loop() — die serielle 
Schnittstelle und stop bleiben jederzeit reaktionsfähig.

--- Schritt-für-Schritt-Programm ---

  prog_clear()                    // Zurücksetzen (optional)
  prog_abs(axis, pos, speed)      // Absolute Position anfahren
  prog_rel(axis, steps, speed)    // Relativ verfahren
  prog_timed(axis, ms, speed)     // Zeitgesteuert fahren
  prog_vibrate(axis, amp, hz)     // Vibrieren
  prog_stop(axis)                 // Achse stoppen
  prog_home(axis)                 // Homing
  prog_enable()                   // Treiber an
  prog_disable()                  // Treiber aus
  prog_servo(num, value)          // Servo positionieren (0-1000)
  prog_wait_ms(ms)                // Warten (non-blocking!)
  prog_wait_axis(axis)            // Warten bis Achse fertig
  prog_wait_all()                 // Warten bis alle fertig
  prog_status()                   // Status-Report ausgeben
  prog_end()                      // Programm-Ende (immer notwendig!)

--- Steuerung ---

  prog_start()                    // Startet Ausführung ab Schritt 0
  prog_stop_exec()                // Bricht laufendes Programm ab
  prog_is_running()               // true wenn Programm läuft

--- Serial-Befehle ---

  run   →  prog_start()
  stop  →  prog_stop_exec()

--- Beispiel: Komplette Hairpin-Sequenz ---

  void setup() {
    // ... I2C init ...
    
    prog_home(AXIS_X);
    prog_wait_axis(AXIS_X);
    
    prog_abs(AXIS_X, 4000, 600);
    prog_wait_axis(AXIS_X);
    
    prog_vibrate(AXIS_Z, 1, 40);
    prog_servo(0, 490);
    prog_wait_ms(2000);
    
    prog_servo(1, 150);
    prog_wait_ms(1000);
    prog_servo(1, 490);
    prog_wait_ms(1000);
    prog_servo(0, 150);
    prog_wait_ms(500);
    
    prog_stop(AXIS_Z);
    prog_abs(AXIS_X, 0, 800);
    prog_wait_axis(AXIS_X);
    
    prog_end();
    prog_start();   // ← Start bei Boot
  }

Der `run`-Befehl startet das gleiche Programm jederzeit neu.
Mit `stop` brichst du jederzeit ab — loop() bleibt live.
*/
