#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define LED_PIN 2  

bool test_mode_active = false;

// --- Ablaufsteuerung (Sequence) ---
enum SequenceState {
  SEQ_IDLE,
  // ── PROGRAMM 1 (Hairpin) ──
  P1_HOME_Z,         P1_HOME_Z_WAIT,
  P1_SERVO_INIT,     P1_SERVO_INIT_WAIT,
  P1_Z_MOVE,         P1_Z_MOVE_WAIT,
  P1_RUN,
  P1_FEED1,          P1_FEED1_WAIT,
  P1_FEED2,          P1_FEED2_WAIT,
  P1_STOP_VIB,
  P1_SERVO_CHANGE,   P1_SERVO_CHANGE_WAIT,
  P1_Y_FORWARD,      P1_Y_FORWARD_WAIT,
  P1_Y_BACK,         P1_Y_BACK_WAIT,
  P1_DONE,
  // ── PROGRAMM 2 (Kopie von P1, zur freien Bearbeitung) ──
  P2_HOME_Z,         P2_HOME_Z_WAIT,
  P2_SERVO_INIT,     P2_SERVO_INIT_WAIT,
  P2_Z_MOVE,         P2_Z_MOVE_WAIT,
  P2_RUN,            P2_WAIT_SLIDE,
  P2_FEED1,          P2_FEED1_WAIT,
  P2_FEED2,          P2_FEED2_WAIT,
  P2_STOP_VIB,
  P2_SERVO_CHANGE,   P2_SERVO_CHANGE_WAIT,
  P2_Y_FORWARD,      P2_Y_FORWARD_WAIT,
  P2_Y_BACK,         P2_Y_BACK_WAIT,
  P2_DONE,
};
SequenceState currentSeqState = SEQ_IDLE;
unsigned long seqStepStartTime = 0;
int seqRemainingRuns = 0;
int currentProgram = 1;  // welches Programm gerade laeuft

// ============================================================================
// HIGH-LEVEL API — Für deine Mini-Programme (siehe setup() Beispiele)
// ============================================================================

// --- Stepper-Funktionen ---
void axis_abs(uint8_t axis, int32_t position, int16_t speed);
void axis_rel(uint8_t axis, int32_t steps, int16_t speed);
void axis_timed(uint8_t axis, unsigned long duration_ms, int16_t speed);
void axis_vibrate(uint8_t axis, int32_t amplitude, int16_t freq_hz);
void axis_stop(uint8_t axis);
void axis_home(uint8_t axis);
void axis_enable();
void axis_disable();

// --- Servo-Funktionen ---
void servo_set(uint8_t num, uint16_t value);  // value: 0-1000

// --- Wait / Status ---
void wait_ms(unsigned long ms);
void wait_axis_busy(uint8_t axis, unsigned long timeout_ms = 30000);
void wait_all_busy(unsigned long timeout_ms = 30000);
bool is_axis_busy(uint8_t axis);          // Pollt UNO-Status
StepperStatus get_stepper_status();        // Holt vollen UNO-Status
void print_status();                       // Detailierter Report

// --- Low-Level (für eigene Erweiterungen) ---
void sendStepperCommand(StepperCommand cmd);
void sendServoCommand(uint8_t num, uint16_t val);

// ============================================================================

void handleSerialMaster();
void scanI2CBus();
void printMasterHelp();
void updateSequence();
void startHairpinSequence(int program, int runs);
void startDefaultSequence(int runs);  // Kurzform fuer Programm 1

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
  Serial.println(F("--------------------------------------------------"));

}

void loop() {
  handleSerialMaster();
  updateSequence();
}

// ============================================================================
// IMPLEMENTIERUNG HIGH-LEVEL API
// ============================================================================

// --- Stepper ---

void axis_abs(uint8_t axis, int32_t position, int16_t speed) {
  StepperCommand cmd = {axis, MOVE_TYPE_ABSOLUTE, position, speed};
  sendStepperCommand(cmd);
}

void axis_rel(uint8_t axis, int32_t steps, int16_t speed) {
  StepperCommand cmd = {axis, MOVE_TYPE_RELATIVE, steps, speed};
  sendStepperCommand(cmd);
}

void axis_timed(uint8_t axis, unsigned long duration_ms, int16_t speed) {
  StepperCommand cmd = {axis, MOVE_TYPE_TIMED, (int32_t)duration_ms, speed};
  sendStepperCommand(cmd);
}

void axis_vibrate(uint8_t axis, int32_t amplitude, int16_t freq_hz) {
  StepperCommand cmd = {axis, MOVE_TYPE_VIBRATE, amplitude, freq_hz};
  sendStepperCommand(cmd);
}

void axis_stop(uint8_t axis) {
  StepperCommand cmd = {axis, MOVE_TYPE_STOP, 0, 0};
  sendStepperCommand(cmd);
}

void axis_home(uint8_t axis) {
  StepperCommand cmd = {axis, MOVE_TYPE_HOMING, 0, 0};
  sendStepperCommand(cmd);
}

void axis_enable() {
  StepperCommand cmd = {0, MOVE_TYPE_STOP, 0, 0};
  sendStepperCommand(cmd);
}

void axis_disable() {
  StepperCommand cmd = {0, MOVE_TYPE_STOP, 99, 0};
  sendStepperCommand(cmd);
}

// --- Servo ---

void servo_set(uint8_t num, uint16_t value) {
  sendServoCommand(num, value);
}

// --- Wait / Status ---

void wait_ms(unsigned long ms) {
  delay(ms);
}

void wait_axis_busy(uint8_t axis, unsigned long timeout_ms) {
  unsigned long start = millis();
  while (true) {
    StepperStatus st = get_stepper_status();
    bool busy = st.axis_busy & (1 << axis);
    if (!busy) return;
    if (timeout_ms > 0 && (millis() - start) >= timeout_ms) {
      Serial.print(F("[TIMEOUT] Achse ")); Serial.print(axis);
      Serial.println(F(" wurde nicht fertig."));
      return;
    }
    delay(10); // Kurze Polling-Pause
  }
}

void wait_all_busy(unsigned long timeout_ms) {
  unsigned long start = millis();
  while (true) {
    StepperStatus st = get_stepper_status();
    if (st.axis_busy == 0) return;
    if (timeout_ms > 0 && (millis() - start) >= timeout_ms) {
      Serial.println(F("[TIMEOUT] Nicht alle Achsen wurden fertig."));
      return;
    }
    delay(10);
  }
}

bool is_axis_busy(uint8_t axis) {
  StepperStatus st = get_stepper_status();
  return (st.axis_busy & (1 << axis)) != 0;
}

StepperStatus get_stepper_status() {
  StepperStatus status = {0, 0, 0, 0, 0};
  uint8_t received = Wire.requestFrom((uint8_t)I2C_ADDR_UNO, (uint8_t)sizeof(StepperStatus));
  if (received >= sizeof(StepperStatus)) {
    Wire.readBytes((uint8_t*)&status, sizeof(StepperStatus));
  }
  return status;
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
// LOW-LEVEL I2C (für Basis-Funktionen)
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

// ============================================================================
// SERIELLE SCHNITTSTELLE (Testmodus)
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

    // run[p<prog>][_<count>]  z.B. run, run5, runp2, runp2_5
    if (input.startsWith("run") || input.startsWith("RUN")) {
      if (currentSeqState != SEQ_IDLE) {
        Serial.println(F("Sequenz laeuft bereits!"));
      } else {
        String arg = input.substring(3); arg.trim();
        int prog = 1, n = 1;
        int pIdx = arg.indexOf('p');
        if (pIdx >= 0) {
          // runp2 oder runp2_5
          String rest = arg.substring(pIdx + 1);
          int uIdx = rest.indexOf('_');
          if (uIdx >= 0) {
            prog = rest.substring(0, uIdx).toInt();
            n = rest.substring(uIdx + 1).toInt();
          } else {
            prog = rest.toInt();
          }
        } else if (arg.length() > 0) {
          // run5 = Programm 1, 5 Durchlaeufe
          n = arg.toInt();
        }
        if (prog < 1) prog = 1; if (prog > 3) prog = 3;
        if (n < 1) n = 1; if (n > 99) n = 99;
        startHairpinSequence(prog, n);
      }
      return;
    }
    
    if (input.equalsIgnoreCase("stop")) {
      Serial.println(F("Sequenz abgebrochen!"));
      currentSeqState = SEQ_IDLE;
      seqRemainingRuns = 0;
      axis_stop(AXIS_X); axis_stop(AXIS_Y); axis_stop(AXIS_Z);
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
  Serial.println(F(" Telemetrie- & Direktbefehle:"));
  Serial.println(F("  status  -> Fordert Live-Meldungen (Positionen/PWM) von beiden Slaves an"));
  Serial.println(F("  enAll   -> Alle Motortreiber EIN"));
  Serial.println(F("  disAll  -> Alle Motortreiber AUS (Motoren stromlos)"));
  Serial.println(F("  scan    -> Scannt den I2C-Bus nach beiden Slaves ab"));
  Serial.println(F("  h       -> Zeigt diese Befehlsmatrix an"));
  Serial.println(F("  t       -> Beendet den Master-Testmodus"));
  Serial.println(F("==================================================================\n"));
}

// ============================================================================
// STATE MACHINE — Deine Sequenz
// ============================================================================
// Ablauf: Z homen → Servos init → Z 200 → 3s Vibration → Servos umschalten
//         → Y 2000 vor/zurueck
//
// Aktionen:
//   axis_home(a)  axis_abs(a,p,s)  axis_rel(a,s,sp)  axis_timed(a,d,s)
//   axis_vibrate(a,amp,hz)  axis_stop(a)  servo_set(n,v)
//   is_axis_busy(a)  axis_enable()  axis_disable()
// ============================================================================

void startHairpinSequence(int program, int runs) {
  seqRemainingRuns = runs;
  currentProgram = program;
  Serial.print(F("Programm ")); Serial.print(program);
  Serial.print(F(" gestartet (")); Serial.print(runs); Serial.println(F(" Durchlauf(e))."));

  switch (program) {
    case 1: currentSeqState = P1_HOME_Z; break;
    case 2: currentSeqState = P2_HOME_Z; break;
    default: currentSeqState = P1_HOME_Z; break;
  }
}

void startDefaultSequence(int runs) {
  startHairpinSequence(1, runs);
}

void updateSequence() {
  if (currentSeqState == SEQ_IDLE) return;
  unsigned long now = millis();

  switch (currentSeqState) {

    // ============================================================
    // PROGRAMM 1: Hairpin-Automation
    //   Z homen → Servos → Z fahren → Vibration → Y hin/zurueck
    // ============================================================

    case P1_HOME_Z:
      Serial.println(F("P1 [1/6] Z homen..."));
      axis_home(AXIS_Z);
      currentSeqState = P1_HOME_Z_WAIT;
      break;

    case P1_HOME_Z_WAIT:
      if (!is_axis_busy(AXIS_Z)) {
        Serial.println(F("  -> Z gehomt."));
        currentSeqState = P1_SERVO_INIT;
      }
      break;

    case P1_SERVO_INIT:
      Serial.println(F("P1 [2/6] Servos: S2=0, S3=800, S4=500"));
      servo_set(0, 1000);
      servo_set(1, 100);
      servo_set(2, 0);
      servo_set(3, 800);
      servo_set(4, 500);
      currentSeqState = P1_SERVO_INIT_WAIT;
      seqStepStartTime = now;
      break;

    case P1_SERVO_INIT_WAIT:
      if (now - seqStepStartTime >= 1500) {
        Serial.println(F("  -> Servos in Position."));
        currentSeqState = P1_Z_MOVE;
      }
      break;

    case P1_Z_MOVE:
      Serial.println(F("P1 [3/6] Z +100 Steps"));
      axis_rel(AXIS_Z, 100, 100);
      currentSeqState = P1_Z_MOVE_WAIT;
      break;

    case P1_Z_MOVE_WAIT:
      if (!is_axis_busy(AXIS_Z)) {
        Serial.println(F("  -> Z positioniert."));
        currentSeqState = P1_RUN;
      }
      break;

    case P1_RUN:
      Serial.println(F("P1 [4/6] Vibration 3s..."));
      axis_vibrate(AXIS_Z, 1, 50);
      currentSeqState = P1_FEED1;
      seqStepStartTime = now;
      break;

    case P1_FEED1:
      servo_set(1, 1000);
      Serial.println(F("  -> Vereinzelung Schritt 1"));
      currentSeqState = P1_FEED1_WAIT;
      break;

    case P1_FEED1_WAIT:
      if (now - seqStepStartTime >= 1500) {
        Serial.println(F("  -> Servo in Position."));
        currentSeqState = P1_FEED2;
      }
      break;

    case P1_FEED2:
      servo_set(0, 100);
      Serial.println(F("  -> Vereinzelung Schritt 2"));
      currentSeqState = P1_FEED2_WAIT;
      break;

    case P1_FEED2_WAIT:
      if (now - seqStepStartTime >= 18000) {
        Serial.println(F("  -> Vereinzelung fertig."));
        currentSeqState = P1_STOP_VIB;
      }
      break;

    case P1_STOP_VIB:
      axis_stop(AXIS_Z);
      Serial.println(F("  -> Vibration gestoppt."));
      currentSeqState = P1_SERVO_CHANGE;
      break;

    case P1_SERVO_CHANGE:
      Serial.println(F("P1 [5/6] Servos: S2=800, S3=0, S4=0"));
      servo_set(2, 800);
      servo_set(3, 0);
      servo_set(4, 0);
      currentSeqState = P1_SERVO_CHANGE_WAIT;
      seqStepStartTime = now;
      break;

    case P1_SERVO_CHANGE_WAIT:
      if (now - seqStepStartTime >= 1500) {
        Serial.println(F("  -> Servos umgeschaltet."));
        currentSeqState = P1_Y_FORWARD;
      }
      break;

    case P1_Y_FORWARD:
      Serial.println(F("P1 [6/6] Y +2000 Steps..."));
      axis_rel(AXIS_Y, 2000, 800);
      currentSeqState = P1_Y_FORWARD_WAIT;
      break;

    case P1_Y_FORWARD_WAIT:
      if (!is_axis_busy(AXIS_Y)) {
        Serial.println(F("  -> Y vor. Y -2000 zurueck..."));
        axis_rel(AXIS_Y, -2000, 800);
        currentSeqState = P1_Y_BACK_WAIT;
      }
      break;

    case P1_Y_BACK_WAIT:
      if (!is_axis_busy(AXIS_Y)) {
        currentSeqState = P1_DONE;
      }
      break;

    case P1_DONE:
      seqRemainingRuns--;
      if (seqRemainingRuns > 0) {
        Serial.print(F("  -> Durchlauf fertig. Noch ")); Serial.print(seqRemainingRuns);
        Serial.print(F("x. Starte P")); Serial.print(currentProgram); Serial.println(F(" neu."));
        currentSeqState = (currentProgram == 1) ? P1_HOME_Z : P2_HOME_Z;
      } else {
        Serial.print(F("Programm ")); Serial.print(currentProgram);
        Serial.println(F(" beendet."));
        currentSeqState = SEQ_IDLE;
      }
      break;

    // ============================================================
    // PROGRAMM 2: Kopie von P1 (Werte selber anpassen!)
    // ============================================================

    case P2_HOME_Z:
      Serial.println(F("P2 [1/6] Z homen..."));
      axis_home(AXIS_Z);
      currentSeqState = P2_HOME_Z_WAIT;
      break;

    case P2_HOME_Z_WAIT:
      if (!is_axis_busy(AXIS_Z)) {
        Serial.println(F("  -> Z gehomt."));
        currentSeqState = P2_SERVO_INIT;
      }
      break;

    case P2_SERVO_INIT:
      Serial.println(F("P2 [2/6] Servos initialisieren..."));
      servo_set(0, 1000);
      servo_set(1, 100);
      servo_set(2, 0);
      servo_set(3, 800);
      servo_set(4, 500);
      currentSeqState = P2_SERVO_INIT_WAIT;
      seqStepStartTime = now;
      break;

    case P2_SERVO_INIT_WAIT:
      if (now - seqStepStartTime >= 1500) {
        Serial.println(F("  -> Servos in Position."));
        currentSeqState = P2_Z_MOVE;
      }
      break;

    case P2_Z_MOVE:
      Serial.println(F("P2 [3/6] Z fahren..."));
      axis_rel(AXIS_Z, 100, 100);
      currentSeqState = P2_Z_MOVE_WAIT;
      break;

    case P2_Z_MOVE_WAIT:
      if (!is_axis_busy(AXIS_Z)) {
        Serial.println(F("  -> Z positioniert."));
        currentSeqState = P2_RUN;
      }
      break;

    case P2_RUN:
      Serial.println(F("P2 [4/6] Vibration..."));
      axis_vibrate(AXIS_Z, 1, 50);
      currentSeqState = P2_WAIT_SLIDE;
      seqStepStartTime = now;
      break;

    case P2_WAIT_SLIDE:
      if (now - seqStepStartTime >= 5000) {
        Serial.println(F("  -> Hairpin rutschen"));
        currentSeqState = P2_FEED1;
        seqStepStartTime = now;
      }
      break;

    case P2_FEED1:
      servo_set(1, 1000);
      Serial.println(F("  -> Vereinzelung 1"));
      currentSeqState = P2_FEED1_WAIT;
      break;

    case P2_FEED1_WAIT:
      if (now - seqStepStartTime >= 1500) {
        Serial.println(F("  -> Servo in Position."));
        currentSeqState = P2_FEED2;
      }
      break;

    case P2_FEED2:
      servo_set(0, 100);
      Serial.println(F("  -> Vereinzelung 2"));
      currentSeqState = P2_FEED2_WAIT;
      break;

    case P2_FEED2_WAIT:
      if (now - seqStepStartTime >= 15000) {
        Serial.println(F("  -> Vereinzelung fertig."));
        currentSeqState = P2_STOP_VIB;
      }
      break;

    case P2_STOP_VIB:
      axis_stop(AXIS_Z);
      Serial.println(F("  -> Vibration aus."));
      currentSeqState = P2_SERVO_CHANGE;
      break;

    case P2_SERVO_CHANGE:
      Serial.println(F("P2 [5/6] Servos umschalten..."));
      servo_set(2, 300);
      servo_set(3, 500);
      servo_set(4, 0);
      currentSeqState = P2_SERVO_CHANGE_WAIT;
      seqStepStartTime = now;
      break;

    case P2_SERVO_CHANGE_WAIT:
      if (now - seqStepStartTime >= 1500) {
        Serial.println(F("  -> Servos umgeschaltet."));
        currentSeqState = P2_Y_FORWARD;
      }
      break;

    case P2_Y_FORWARD:
      Serial.println(F("P2 [6/6] Y vor..."));
      axis_rel(AXIS_Y, 4000, 2000);
      currentSeqState = P2_Y_FORWARD_WAIT;
      break;

    case P2_Y_FORWARD_WAIT:
      if (!is_axis_busy(AXIS_Y)) {
        Serial.println(F("  -> Y vor. Y zurueck..."));
        axis_rel(AXIS_Y, -4000, 2000);
        currentSeqState = P2_Y_BACK_WAIT;
      }
      break;

    case P2_Y_BACK_WAIT:
      if (!is_axis_busy(AXIS_Y)) {
        currentSeqState = P2_DONE;
      }
      break;

    case P2_DONE:
      seqRemainingRuns--;
      if (seqRemainingRuns > 0) {
        Serial.print(F("  -> Durchlauf fertig. Noch ")); Serial.print(seqRemainingRuns);
        Serial.println(F("x. Starte P2 neu."));
        currentSeqState = P2_HOME_Z;
      } else {
        Serial.println(F("Programm 2 beendet."));
        currentSeqState = SEQ_IDLE;
      }
      break;

    default:
      currentSeqState = SEQ_IDLE;
      break;
  }
}

/*
=== HIGH-LEVEL API REFERENZ ===

Du schreibst dein Mini-Programm in die setup()-Funktion.
Hier alle verfügbaren Befehle:

--- Stepper (Achsen: AXIS_X, AXIS_Y, AXIS_Z) ---

  axis_abs(axis, position, speed)
    Fährt Achse auf absolute Position (Steps).
    axis_abs(AXIS_X, 5000, 800);

  axis_rel(axis, steps, speed)
    Fährt Achse relativ vom aktuellen Punkt.
    axis_rel(AXIS_X, -1000, 600);  // 1000 Steps zurück

  axis_timed(axis, duration_ms, speed)
    Fährt für eine bestimmte Zeit (ms).
    axis_timed(AXIS_X, 3000, -500);  // 3s rückwärts mit 500 Steps/s

  axis_vibrate(axis, amplitude, freq_hz)
    Vibriert mit Amplitude (Steps) und Frequenz (Hz).
    axis_vibrate(AXIS_Z, 1, 40);

  axis_stop(axis)
    Stoppt eine Achse sofort.
    axis_stop(AXIS_X);

  axis_home(axis)
    Startet Homing-Fahrt (Endschalter-Suche).
    axis_home(AXIS_X);

  axis_enable()
    Motortreiber einschalten (Haltestrom).

  axis_disable()
    Motortreiber ausschalten (stromlos).

--- Servos (Nummern: 0-5) ---

  servo_set(num, value)
    Setzt Servo auf Wert 0-1000.
    (0=min 500us, 1000=max 2500us — es sei denn, INVERT ist gesetzt)
    servo_set(0, 490);

--- Warten & Status ---

  wait_ms(ms)
    Wartet für ms Millisekunden.
    wait_ms(500);

  wait_axis_busy(axis, timeout_ms = 30000)
    Wartet, bis die Achse ihre Fahrt beendet hat.
    wait_axis_busy(AXIS_X);        // unbegrenzt warten
    wait_axis_busy(AXIS_X, 5000);  // max 5s warten

  wait_all_busy(timeout_ms = 30000)
    Wartet, bis ALLE Achsen fertig sind.
    wait_all_busy();

  is_axis_busy(axis)
    Prüft, ob eine Achse noch fährt. true/false.

  print_status()
    Zeigt alle Positionen, Servo-Werte und Sensordaten an.

--- Beispiel: Eigene Sequenz in updateSequence() ---

  // Neuen enum-Wert eintragen, z.B. SEQ_MY_HOME
  // Dann in updateSequence() ein case hinzufuegen:
  //
  //   case SEQ_MY_HOME:
  //     axis_home(AXIS_X);
  //     currentSeqState = SEQ_MY_MOVE;
  //     seqStepStartTime = now;
  //     break;
  //
  //   case SEQ_MY_MOVE:
  //     if (!is_axis_busy(AXIS_X)) {
  //       axis_abs(AXIS_X, 5000, 800);
  //       currentSeqState = SEQ_MY_DONE;
  //     }
  //     break;
  //
  // Mit `run` starten, `stop` bricht ab.
*/


