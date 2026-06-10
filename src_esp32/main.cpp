#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define LED_PIN 2  

bool test_mode_active = false;

// --- Ablaufsteuerung (Sequence) ---
enum SequenceState { SEQ_IDLE, SEQ_VIB_START, SEQ_SERVO0_1, SEQ_SERVO1_1, SEQ_SERVO1_2, SEQ_SERVO0_2, SEQ_VIB_STOP };
SequenceState currentSeqState = SEQ_IDLE;
unsigned long seqStepStartTime = 0;
const unsigned long SERVO_WAIT_TIME = 2000; // 1 Sekunde warten, bis Servos ihre Position erreicht haben

void handleSerialMaster();
void sendStepperCommand(StepperCommand cmd);
void sendServoCommand(uint8_t num, uint16_t val);
void scanI2CBus();
void printMasterHelp();
void updateSequence();
void startHairpinSequence();

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
  
  if (error != 0 && test_mode_active) {
    Serial.print(F("[I2C SERVO FEHLER] Code: ")); Serial.println(error);
  }
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
      else Serial.println();
      nDevices++;
    }
  }
  if (nDevices == 0) Serial.println(F("WARNUNG: Keine I2C-Geraete gefunden!\n"));
}

void requestAndPrintStatus() {
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
  }
  
  Serial.println(F("-------------------------------------------------------"));

  // 2. Nano ABFRAGE TYP 1: Servo-Stellungen (0-1000)
  Wire.beginTransmission(I2C_ADDR_NANO); //
  Wire.write(REQ_NANO_SERVOS);           // Dem Nano sagen: "Ich will Servo-Werte!"
  Wire.endTransmission();
  
  Wire.requestFrom(I2C_ADDR_NANO, sizeof(ServoStatus)); //
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
  Wire.beginTransmission(I2C_ADDR_NANO); //
  Wire.write(REQ_NANO_SENSORS);          // Dem Nano sagen: "Ich will jetzt Sensor-Daten!"
  Wire.endTransmission();
  
  Wire.requestFrom(I2C_ADDR_NANO, sizeof(SensorStatus)); //
  if (Wire.available() >= (int)sizeof(SensorStatus)) {
    SensorStatus snStatus;
    Wire.readBytes((uint8_t*)&snStatus, sizeof(SensorStatus));
    
    // Umrechnung Raw-ADC (0-1023) in Volt für die analogen Pins (5V Referenz beim Nano)
    float volt_a1 = (snStatus.analog_a1 * 5.0) / 1023.0;
    float volt_a2 = (snStatus.analog_a2 * 5.0) / 1023.0;
    float volt_a3 = (snStatus.analog_a3 * 5.0) / 1023.0;
    float volt_mh = (snStatus.mh_a7_raw * 5.0) / 1023.0;

    Serial.println(F("[SLAVE 2: ARDUINO NANO (0x32) - SENSOREN & SPANNUNGEN]"));
    Serial.print(F("  -> Shunt Strommessung (A0 Raw) : ")); Serial.println(snStatus.shunt_raw); //
    Serial.print(F("  -> MH-Sensor Digital (D2)      : ")); Serial.println(snStatus.mh_d2_state == HIGH ? F("HIGH") : F("LOW")); //
    Serial.print(F("  -> MH-Sensor Analog (A7)       : ")); Serial.print(volt_mh); Serial.println(F(" V")); //
    Serial.print(F("  -> Spannung Pin A1             : ")); Serial.print(volt_a1); Serial.println(F(" V"));
    Serial.print(F("  -> Spannung Pin A2             : ")); Serial.print(volt_a2); Serial.println(F(" V"));
    Serial.print(F("  -> Spannung Pin A3             : ")); Serial.print(volt_a3); Serial.println(F(" V"));
  }
  Serial.println(F("========================================================\n"));
}

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
    if (input.equalsIgnoreCase("status")) { requestAndPrintStatus(); return; } // NEUER BEFEHL
    if (input.equalsIgnoreCase("run")) { 
      Serial.println(F("\n[MASTER] Starte Hairpin-Zuführung-Sequenz..."));
      startHairpinSequence(); 
      return; 
    }
    
    if (input.equalsIgnoreCase("stop")) { 
      Serial.println(F("\n[MASTER] ABBRUCH: Sequenz gestoppt!"));
      currentSeqState = SEQ_IDLE;
      sendStepperCommand({AXIS_X, MOVE_TYPE_STOP, 0, 0}); // Sofort-Stopp der Vibration
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

void startHairpinSequence() {
  if (currentSeqState == SEQ_IDLE) {
    currentSeqState = SEQ_VIB_START;
  } else {
    Serial.println(F("[WARNUNG] Sequenz laeuft bereits!"));
  }
}

void updateSequence() {
  if (currentSeqState == SEQ_IDLE) return;

  unsigned long now = millis();

  switch (currentSeqState) {
    case SEQ_VIB_START:
      Serial.println(F("  -> Schritt 1: Vibration starten"));
      sendStepperCommand({AXIS_Z, MOVE_TYPE_VIBRATE, 1, 40}); 
      currentSeqState = SEQ_SERVO0_1;
      seqStepStartTime = now;
      break;

    case SEQ_SERVO0_1:
      if (now - seqStepStartTime >= 1000) { // Kurze Verzögerung, damit Vibration wirkt
        Serial.println(F("  -> Schritt 2: Servo 0 auf MAX"));
        sendServoCommand(0, 490); // 490 ist das Max-Limit laut Nano Config
        currentSeqState = SEQ_SERVO1_1;
        seqStepStartTime = now;
      }
      break;

    case SEQ_SERVO1_1:
      if (now - seqStepStartTime >= SERVO_WAIT_TIME) {
        Serial.println(F("  -> Schritt 3: Servo 1 auf MIN"));
        sendServoCommand(1, 150);
        currentSeqState = SEQ_SERVO1_2;
        seqStepStartTime = now;
      }
      break;

    case SEQ_SERVO1_2:
      if (now - seqStepStartTime >= SERVO_WAIT_TIME) {
        Serial.println(F("  -> Schritt 4: Servo 1 auf MAX"));
        sendServoCommand(1, 490);
        currentSeqState = SEQ_SERVO0_2;
        seqStepStartTime = now;
      }
      break;

    case SEQ_SERVO0_2:
      if (now - seqStepStartTime >= SERVO_WAIT_TIME) {
        Serial.println(F("  -> Schritt 4: Servo 0 auf MIN"));
        sendServoCommand(0, 150);
        currentSeqState = SEQ_VIB_STOP;
        seqStepStartTime = now;
      }
      break;

    case SEQ_VIB_STOP:
      if (now - seqStepStartTime >= SERVO_WAIT_TIME) {
        Serial.println(F("  -> Schritt 4: Vibration stoppen"));
        sendStepperCommand({AXIS_Z, MOVE_TYPE_STOP, 0, 0});
        Serial.println(F("[SEQUENZ BEENDET]"));
        currentSeqState = SEQ_IDLE;
      }
      break;
  }
}

/*
=== BENUTZERHANDBUCH FÜR DIE SERIELLE KONSOLE ===

1. System Start/Stop:
   - 't'      : Schaltet den Testmodus (Master-Kontrolle) AN oder AUS.

2. Automatischer Ablauf:
   - 'run'    : Startet die vordefinierte Sequenz (Vibration -> S0 -> S1 -> Stop).
   - 'stop'   : Bricht die laufende Sequenz sofort ab und stoppt die Vibration.

3. Status & Diagnose:
   - 'status' : Fragt Positionen vom Uno und Sensorwerte vom Nano ab.
   - 'scan'   : Scannt den I2C-Bus nach Slaves (Uno: 0x33, Nano: 0x32).

4. Treiber-Kontrolle:
   - 'enAll'  : Aktiviert alle Motortreiber (Motoren unter Haltestrom).
   - 'disAll' : Deaktiviert alle Treiber (Motoren stromlos / frei beweglich).

5. Manuelle Stepper-Befehle (STP_[Achse],[Typ],[P1],[P2]):
   - STP_X,REL,1000,500 : Bewege X relativ 1000 Steps mit Speed 500.
   - STP_Y,ABS,0,800    : Fahre Y auf Nullposition mit Speed 800.
   - STP_Z,HOMING,0,0   : Startet Homing-Fahrt der Z-Achse.
   - STP_X,VIB,5,50     : Vibriere X mit Amplitude 5 und 50Hz.

6. Manuelle Servo-Befehle (SRV_[Nummer],[WERT]):
   - SRV_0,500 : Setzt Servo 0 auf den Wert 500 (Bereich 0-1000).
   - SRV_1,900 : Setzt Servo 1 auf den Wert 900 (Max-Limit).
*/