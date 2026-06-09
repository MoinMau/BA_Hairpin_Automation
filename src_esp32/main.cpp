#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define LED_PIN 2  

bool test_mode_active = false;

void handleSerialMaster();
void sendI2CCommand(StepperCommand cmd);
void scanI2CBus();
void printMasterHelp();

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
}

void sendI2CCommand(StepperCommand cmd) {
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

    if (input.equalsIgnoreCase("enAll")) {
      StepperCommand cmd = {0, MOVE_TYPE_STOP, 0, 0};
      digitalWrite(LED_PIN, HIGH);
      Wire.beginTransmission(I2C_ADDR_UNO);
      Wire.write((uint8_t*)&cmd, sizeof(StepperCommand));
      Wire.endTransmission();
      digitalWrite(LED_PIN, LOW);
      return;
    }
    if (input.equalsIgnoreCase("disAll")) {
      StepperCommand cmd = {0, MOVE_TYPE_STOP, 99, 0};
      digitalWrite(LED_PIN, HIGH);
      Wire.beginTransmission(I2C_ADDR_UNO);
      Wire.write((uint8_t*)&cmd, sizeof(StepperCommand));
      Wire.endTransmission();
      digitalWrite(LED_PIN, LOW);
      return;
    }

    // --- NEU: Verzweigung zwischen Schrittmotor (Uno) und Servo (Nano) ---
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
        else { Serial.println(F("!! Fehler: Achse unbekannt !!")); return; }

        if (typeStr == "REL") cmd.move_type = MOVE_TYPE_RELATIVE;
        else if (typeStr == "ABS") cmd.move_type = MOVE_TYPE_ABSOLUTE;
        else if (typeStr == "TIMED") cmd.move_type = MOVE_TYPE_TIMED;
        else if (typeStr == "VIB") cmd.move_type = MOVE_TYPE_VIBRATE;
        else if (typeStr == "STOP") cmd.move_type = MOVE_TYPE_STOP;
        else if (typeStr == "HOMING") cmd.move_type = MOVE_TYPE_HOMING;
        else { Serial.println(F("!! Fehler: Unbekannter Typ !!")); return; }

        cmd.parameter1 = p1; cmd.parameter2 = p2;

        digitalWrite(LED_PIN, HIGH);
        Wire.beginTransmission(I2C_ADDR_UNO); // An Uno senden!
        Wire.write((uint8_t*)&cmd, sizeof(StepperCommand));
        uint8_t err = Wire.endTransmission();
        digitalWrite(LED_PIN, LOW);
        if (err == 0) Serial.println(F("[I2C Master] Stepper-Befehl an Uno übermittelt."));
      } else {
        Serial.println(F("Syntax-Fehler Stepper! Format: STP_X,REL,1000,500"));
      }
    }
    else if (input.startsWith("SRV_") || input.startsWith("srv_")) {
      // Format für Servos: SRV_[ServoNum],[PWM] -> z.B. SRV_0,1500
      int firstComma = input.indexOf(',');
      if (firstComma != -1) {
        uint8_t s_num = input.substring(4, firstComma).toInt(); // Extrahiert die Nummer hinter "SRV_"
        uint16_t pwm = input.substring(firstComma + 1).toInt();

        ServoCommand cmd = {s_num, pwm};

        digitalWrite(LED_PIN, HIGH);
        Wire.beginTransmission(I2C_ADDR_NANO); // An Nano senden!
        Wire.write((uint8_t*)&cmd, sizeof(ServoCommand));
        uint8_t err = Wire.endTransmission();
        digitalWrite(LED_PIN, LOW);
        if (err == 0) Serial.println(F("[I2C Master] Servo-Befehl an Nano übermittelt."));
      } else {
        Serial.println(F("Syntax-Fehler Servo! Format: SRV_0,1500"));
      }
    } else {
      Serial.println(F("Unbekannter Befehl! Nutze das Präfix STP_ oder SRV_"));
    }
  }
}

// Ersetze das Hilfemenü im ESP32-Code für die vollständige Übersicht:
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
  Serial.println(F("  (Servos: 0-5  |  PWM-Sicherheitsbereich: 500 bis 2500)"));
  Serial.println(F(" -----------------------------------------------------------------"));
  Serial.println(F(" Globale Direktbefehle:"));
  Serial.println(F("  enAll   -> Alle Motortreiber EIN"));
  Serial.println(F("  disAll  -> Alle Motortreiber AUS (Motoren stromlos)"));
  Serial.println(F("  scan    -> Scannt den I2C-Bus nach beiden Slaves ab"));
  Serial.println(F("  h       -> Zeigt diese Befehlsmatrix an"));
  Serial.println(F("  t       -> Beendet den Master-Testmodus"));
  Serial.println(F("==================================================================\n"));
}