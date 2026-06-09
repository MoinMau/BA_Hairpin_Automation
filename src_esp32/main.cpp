#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define LED_PIN 2  

bool test_mode_active = false;

void handleSerialMaster();
void sendI2CCommand(StepperCommand cmd);
void sendHomingXSignal();
void scanI2CBus();
void printMasterHelp();

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT); 
  digitalWrite(LED_PIN, LOW);
  
  Wire.begin(I2C_SDA, I2C_SCL, 100000); 
  
  // Begrüßung beim Start
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
    Serial.print(F("[I2C FEHLER] Befehl blockiert. Code: ")); Serial.println(error);
  } else {
    Serial.println(F("[I2C SUCCESS] Daten erfolgreich an Uno uebertragen."));
  }
}

void sendHomingXSignal() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println(F("[Master] Sende Homing-Signal (CMD_STEPPER_HOMING) an Uno..."));
  Wire.beginTransmission(I2C_ADDR_UNO);
  Wire.write(CMD_STEPPER_HOMING);
  uint8_t error = Wire.endTransmission();
  digitalWrite(LED_PIN, LOW);
  
  if (error != 0) {
    Serial.print(F("[I2C FEHLER] Homing-Signal fehlgeschlagen. Code: ")); Serial.println(error);
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
  else Serial.println(F("Scan beendet.\n"));
}

void handleSerialMaster() {
  if (Serial.available() > 0) {
    char ch = Serial.peek();
    
    if (ch == 't' || ch == 'T') {
      Serial.read(); 
      test_mode_active = !test_mode_active;
      Serial.println(F("\n=================================================="));
      Serial.print(F(" MASTER-KONTROLLE: ")); Serial.println(test_mode_active ? F("ONLINE (Bereit)") : F("STANDBY (Gesperrt)"));
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
    if (input.equalsIgnoreCase("homeX")) { sendHomingXSignal(); return; }

    // --- NEU: Globale Treiber-Zustände abfangen ---
    if (input.equalsIgnoreCase("enAll")) {
      Serial.println(F("[Master] Befehl: Aktiviere alle Motortreiber (enAll)"));
      StepperCommand cmd = {0, MOVE_TYPE_FREEZE, 0, 0}; // Ein Freeze-Befehl reaktiviert Treiber im Normalbetrieb
      sendI2CCommand(cmd);
      return;
    }
    if (input.equalsIgnoreCase("disAll")) {
      Serial.println(F("[Master] Befehl: DEAKTIVIERE alle Motortreiber (disAll) -> Motoren stromlos!"));
      // Um disAll sauber zu tunneln, senden wir einen FREEZE-Befehl mit einem speziellen Flag (z.B. Parameter1 = 99)
      StepperCommand cmd = {0, MOVE_TYPE_FREEZE, 99, 0}; 
      sendI2CCommand(cmd);
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

      StepperCommand cmd;
      
      if (axisStr == "STP_X") cmd.axis = AXIS_X;
      else if (axisStr == "STP_Y") cmd.axis = AXIS_Y;
      else if (axisStr == "STP_Z") cmd.axis = AXIS_Z;
      else { Serial.println(F("!! Fehler: Achse unbekannt (STP_X, STP_Y, STP_Z) !!")); return; }

      if (typeStr == "REL") cmd.move_type = MOVE_TYPE_RELATIVE;
      else if (typeStr == "ABS") cmd.move_type = MOVE_TYPE_ABSOLUTE;
      else if (typeStr == "TIMED") cmd.move_type = MOVE_TYPE_TIMED;
      else if (typeStr == "VIB") cmd.move_type = MOVE_TYPE_VIBRATE;
      else if (typeStr == "FREEZE") cmd.move_type = MOVE_TYPE_FREEZE;
      else { Serial.println(F("!! Fehler: Unbekannter Bewegungstyp !!")); return; }

      cmd.parameter1 = p1;
      cmd.parameter2 = p2;

      sendI2CCommand(cmd);
    } else {
      Serial.println(F("Syntax-Fehler! Nutze das Format: STP_X,REL,1000,500"));
    }
  }
}

void printMasterHelp() {
  Serial.println(F(" OOOOO  BEFEHLS-SYNTAX: [Achse],[Modus],[Param1],[Param2]"));
  Serial.println(F(" -----------------------------------------------------------------"));
  Serial.println(F("  STP_X,REL,2000,800     -> Relative Fahrt (2000 Schritte, 800 Steps/s)"));
  Serial.println(F("  STP_Y,ABS,4000,600     -> Absolute Koordinate (Zielschritt 4000, 600 Steps/s)"));
  Serial.println(F("  STP_Z,TIMED,5000,-300  -> Zeitfahrt (5000 ms lang, mit -300 Steps/s rückwärts)"));
  Serial.println(F("  STP_X,VIB,6,45         -> Vibration (6 Steps Amplitude, 45 Hz Ruttelfrequenz)"));
  Serial.println(F("  STP_X,FREEZE,0,0       -> Notstopp Achse X: Friert aktuelle Position ein"));
  Serial.println(F(" -----------------------------------------------------------------"));
  Serial.println(F(" OOOOO  SYSTEM-SPEZIALBEFEHLE (Direkte Eingabe ohne Komma):"));
  Serial.println(F(" -----------------------------------------------------------------"));
  Serial.println(F("  homeX   -> Startet automatische X-Kalibrierung am Endstopp"));
  Serial.println(F("  enAll   -> Schaltet alle Motortreiber EIN (Motoren unter Haltestrom)"));
  Serial.println(F("  disAll  -> Schaltet alle Motortreiber AUS (Motoren komplett stromlos/frei)"));
  Serial.println(F("  scan    -> Scannt den I2C-Bus nach dem Arduino Uno ab"));
  Serial.println(F("  h       -> Zeigt diese Befehlsübersicht erneut an"));
  Serial.println(F("  t       -> Beendet den Master-Testmodus (Standby)"));
  Serial.println(F("=================================================="));
}