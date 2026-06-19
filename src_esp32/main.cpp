#include <Arduino.h>
#include <Wire.h>
#include "i2c_protocol.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define LED_PIN 2

bool test_mode_active = false;

// =============================================================================
// PROGRAMM-ENGINE — Non-Blocking Step-Ausführung
// =============================================================================
// Schritt-Typen
enum ProgramCmd : uint8_t {
  CMD_ABS,          // Absolute Position   axis, position,    speed
  CMD_REL,          // Relativ verfahren   axis, steps,       speed
  CMD_TIMED,        // Zeitfahrt           axis, dauer_ms,    speed
  CMD_VIB,          // Vibration           axis, amplitude,   frequenz_hz
  CMD_STOP,         // Achse stoppen       axis, -,           -
  CMD_HOME,         // Homing              axis, -,           -
  CMD_ENABLE,       // Treiber an          -, -,              -
  CMD_DISABLE,      // Treiber aus         -, -,              -
  CMD_SERVO,        // Servo setzen        servo_nr, wert,    -
  CMD_WAIT_MS,      // Warten (ms)         -, ms,             -
  CMD_WAIT_AXIS,    // Warten auf Achse    axis, -,           -
  CMD_WAIT_ALL,     // Warten auf alle     -, -,              -
  CMD_STATUS,       // Status ausgeben     -, -,              -
  CMD_END,          // Programm-Ende       -, -,              -
};

struct ProgramStep {
  ProgramCmd cmd;
  uint8_t axis;
  int32_t param1;
  int16_t param2;
};

// =============================================================================
// MAKROS: Saubere Definition deines Programms
// =============================================================================
// Einfach in setup() (oder einer eigenen Funktion) eine Liste schreiben.
//
//   PROGRAM(
//     HOME(AXIS_X),                  // Homing X starten
//     WAIT_AXIS(AXIS_X),             // Warten bis X fertig
//     ABS(AXIS_X, 5000, 800),        // X auf Position 5000, Speed 800
//     WAIT_AXIS(AXIS_X),             // Warten bis angekommen
//     SERVO(0, 490),                 // Servo 0 auf Wert 490
//     WAIT_MS(2000),                 // 2 Sekunden warten
//     VIBRATE(AXIS_Z, 1, 40),        // Z vibrieren lassen (1 Step, 40 Hz)
//     WAIT_MS(1000),
//     STOP(AXIS_Z),                  // Z stoppen
//     ABS(AXIS_X, 0, 800),           // X zurück auf 0
//     WAIT_AXIS(AXIS_X),
//     END                            // Programm-Ende (wichtig!)
//   );
//
// Start mit:  prog_start()   oder   run (Serial)
// Abbruch:    prog_stop()    oder   stop (Serial)
// =============================================================================

#define HOME(a)       ProgramStep{CMD_HOME, a, 0, 0}
#define ABS(a,p,s)    ProgramStep{CMD_ABS, a, p, s}
#define REL(a,s,sp)   ProgramStep{CMD_REL, a, s, sp}
#define TIMED(a,d,s)  ProgramStep{CMD_TIMED, a, (int32_t)d, s}
#define VIBRATE(a,a2,f) ProgramStep{CMD_VIB, a, a2, f}
#define STOP(a)       ProgramStep{CMD_STOP, a, 0, 0}
#define ENABLE        ProgramStep{CMD_ENABLE, 0, 0, 0}
#define DISABLE       ProgramStep{CMD_DISABLE, 0, 0, 0}
#define SERVO(n,v)    ProgramStep{CMD_SERVO, n, v, 0}
#define WAIT_MS(ms)   ProgramStep{CMD_WAIT_MS, 0, (int32_t)ms, 0}
#define WAIT_AXIS(a)  ProgramStep{CMD_WAIT_AXIS, a, 0, 0}
#define WAIT_ALL      ProgramStep{CMD_WAIT_ALL, 0, 0, 0}
#define STATUS        ProgramStep{CMD_STATUS, 0, 0, 0}
#define END           ProgramStep{CMD_END, 0, 0, 0}

// Programm in den Buffer kopieren
#define PROGRAM(...) do { \
  static const ProgramStep __p[] = {__VA_ARGS__}; \
  program_count = sizeof(__p) / sizeof(__p[0]); \
  memcpy(program_buffer, __p, sizeof(__p)); \
} while(0)

// =============================================================================
// INTERNE PROGRAMM-VERWALTUNG
// =============================================================================

#define MAX_PROGRAM_STEPS 64
static ProgramStep program_buffer[MAX_PROGRAM_STEPS];
static int program_count = 0;
static int program_index = -1;       // -1 = idle
static unsigned long program_wait_start = 0;
static unsigned long program_poll_last = 0;
static const unsigned long POLL_INTERVAL_MS = 50;

// Forward-Deklarationen (Definitionen weiter unten)
void sendStepperCommand(StepperCommand cmd);
void sendServoCommand(uint8_t num, uint16_t val);
void print_status();
void handleSerialMaster();
void printMasterHelp();
void scanI2CBus();

// =============================================================================
// PROGRAMM-STEUERUNG (öffentlich)
// =============================================================================

// Programm starten (reset auf Schritt 0)
void prog_start() {
  if (program_count == 0) {
    Serial.println(F("[PROG] Kein Programm definiert!"));
    return;
  }
  program_index = 0;
  program_wait_start = millis();
  Serial.println(F("[PROG] Programm gestartet."));
}

// Programm sofort abbrechen
void prog_stop() {
  if (program_index >= 0) {
    Serial.println(F("[PROG] Programm abgebrochen."));
    program_index = -1;
    sendStepperCommand({AXIS_X, MOVE_TYPE_STOP, 0, 0});
    sendStepperCommand({AXIS_Y, MOVE_TYPE_STOP, 0, 0});
    sendStepperCommand({AXIS_Z, MOVE_TYPE_STOP, 0, 0});
  }
}

bool prog_is_running() { return program_index >= 0; }

// Ein externes Programm-Array laden und starten
void run_program(const ProgramStep* steps, int count) {
  if (count > MAX_PROGRAM_STEPS) count = MAX_PROGRAM_STEPS;
  memcpy(program_buffer, steps, count * sizeof(ProgramStep));
  program_count = count;
  prog_start();
}

// =============================================================================
// PROGRAMM-AUSFÜHRUNG (ein Schritt pro loop()-Aufruf)
// =============================================================================

static void exec_cmd(const ProgramStep& s) {
  switch (s.cmd) {
    case CMD_ABS:    sendStepperCommand({s.axis, MOVE_TYPE_ABSOLUTE, s.param1, s.param2}); break;
    case CMD_REL:    sendStepperCommand({s.axis, MOVE_TYPE_RELATIVE, s.param1, s.param2}); break;
    case CMD_TIMED:  sendStepperCommand({s.axis, MOVE_TYPE_TIMED,   s.param1, s.param2}); break;
    case CMD_VIB:    sendStepperCommand({s.axis, MOVE_TYPE_VIBRATE, s.param1, s.param2}); break;
    case CMD_STOP:   sendStepperCommand({s.axis, MOVE_TYPE_STOP,    0,        0});         break;
    case CMD_HOME:   sendStepperCommand({s.axis, MOVE_TYPE_HOMING,  0,        0});         break;
    case CMD_ENABLE: sendStepperCommand({0,      MOVE_TYPE_STOP,    0,        0});         break;
    case CMD_DISABLE:sendStepperCommand({0,      MOVE_TYPE_STOP,    99,       0});         break;
    case CMD_SERVO:  sendServoCommand(s.axis, (uint16_t)s.param1);                        break;
    case CMD_WAIT_MS:
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
      return;
    default:
      break;
  }
  program_index++;
}

static bool wait_done(const ProgramStep& s) {
  if (millis() - program_poll_last < POLL_INTERVAL_MS) return false;
  program_poll_last = millis();

  if (s.cmd == CMD_WAIT_MS) {
    return (millis() - program_wait_start) >= (unsigned long)s.param1;
  }

  uint8_t r = Wire.requestFrom((uint8_t)I2C_ADDR_UNO, (uint8_t)sizeof(StepperStatus));
  if (r < sizeof(StepperStatus)) return false;
  StepperStatus st;
  Wire.readBytes((uint8_t*)&st, sizeof(StepperStatus));

  if (s.cmd == CMD_WAIT_AXIS) return (st.axis_busy & (1 << s.axis)) == 0;
  if (s.cmd == CMD_WAIT_ALL)  return st.axis_busy == 0;
  return true;
}

void prog_update() {
  if (program_index < 0 || program_index >= program_count) return;
  ProgramStep& cur = program_buffer[program_index];

  if (cur.cmd == CMD_WAIT_MS || cur.cmd == CMD_WAIT_AXIS || cur.cmd == CMD_WAIT_ALL) {
    if (wait_done(cur)) program_index++;
    return;
  }
  exec_cmd(cur);
}

// =============================================================================
// I2C-KOMMANDOS AN SLAVES
// =============================================================================

void sendStepperCommand(StepperCommand cmd) {
  digitalWrite(LED_PIN, HIGH);
  Wire.beginTransmission(I2C_ADDR_UNO);
  Wire.write((uint8_t*)&cmd, sizeof(StepperCommand));
  uint8_t error = Wire.endTransmission();
  digitalWrite(LED_PIN, LOW);
  if (error != 0) {
    Serial.print(F("[I2C FEHLER] Code: ")); Serial.println(error);
  } else {
    Serial.println(F("[I2C SUCCESS] Befehl an Uno."));
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

// =============================================================================
// HILFS-FUNKTIONEN
// =============================================================================

StepperStatus get_stepper_status() {
  StepperStatus st = {0, 0, 0, 0, 0};
  uint8_t r = Wire.requestFrom((uint8_t)I2C_ADDR_UNO, (uint8_t)sizeof(StepperStatus));
  if (r >= sizeof(StepperStatus)) Wire.readBytes((uint8_t*)&st, sizeof(StepperStatus));
  return st;
}

bool is_axis_busy(uint8_t axis) {
  return (get_stepper_status().axis_busy & (1 << axis)) != 0;
}

void scanI2CBus() {
  Serial.println(F("\n--- I2C-Bus-Scan ---"));
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  Geraet 0x")); if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      if (addr == I2C_ADDR_UNO)  Serial.println(F(" (Uno Stepper)"));
      else if (addr == I2C_ADDR_NANO) Serial.println(F(" (Nano Servo)"));
      else Serial.println();
    }
  }
}

void print_status() {
  Serial.println(F("\n=== SYSTEM STATUS ==="));
  // Uno
  uint8_t r = Wire.requestFrom((uint8_t)I2C_ADDR_UNO, (uint8_t)sizeof(StepperStatus));
  if (r >= sizeof(StepperStatus)) {
    StepperStatus st;
    Wire.readBytes((uint8_t*)&st, sizeof(StepperStatus));
    Serial.print(F("Uno: X=")); Serial.print(st.current_pos_x);
    Serial.print(F(" Y=")); Serial.print(st.current_pos_y);
    Serial.print(F(" Z=")); Serial.print(st.current_pos_z);
    Serial.print(F(" | Homing=")); Serial.print(st.homing_active ? 1 : 0);
    Serial.print(F(" | Busy="));
    Serial.print(st.axis_busy & BUSY_X ? 'X' : '.'); Serial.print(st.axis_busy & BUSY_Y ? 'Y' : '.');
    Serial.println(st.axis_busy & BUSY_Z ? 'Z' : '.');
  }
  // Nano Servos
  Wire.beginTransmission(I2C_ADDR_NANO); Wire.write(REQ_NANO_SERVOS); Wire.endTransmission();
  Wire.requestFrom(I2C_ADDR_NANO, sizeof(ServoStatus));
  if (Wire.available() >= (int)sizeof(ServoStatus)) {
    ServoStatus ns;
    Wire.readBytes((uint8_t*)&ns, sizeof(ServoStatus));
    Serial.print(F("Nano Servos:"));
    for (int i = 0; i < 6; i++) { Serial.print(F(" S")); Serial.print(i); Serial.print('='); Serial.print(ns.current_val[i]); }
    Serial.println();
  }
  // Nano Sensoren
  Wire.beginTransmission(I2C_ADDR_NANO); Wire.write(REQ_NANO_SENSORS); Wire.endTransmission();
  Wire.requestFrom(I2C_ADDR_NANO, sizeof(SensorStatus));
  if (Wire.available() >= (int)sizeof(SensorStatus)) {
    SensorStatus sn; Wire.readBytes((uint8_t*)&sn, sizeof(SensorStatus));
    Serial.print(F("Nano Sensoren: Shunt=")); Serial.print(sn.shunt_raw);
    Serial.print(F(" MH-D=")); Serial.print(sn.mh_d2_state);
    Serial.print(F(" MH-A=")); Serial.print(sn.mh_a7_raw);
    Serial.print(F(" A1=")); Serial.print(sn.analog_a1);
    Serial.print(F(" A2=")); Serial.print(sn.analog_a2);
    Serial.print(F(" A3=")); Serial.println(sn.analog_a3);
  }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Wire.begin(I2C_SDA, I2C_SCL, 100000);

  Serial.println(F("\n=== ESP32 I2C Master ==="));
  Serial.println(F("t        = Testmodus ein/aus"));
  Serial.println(F("run/stop = Programm starten/stoppen"));
  Serial.println(F("h        = Hilfe"));

  // ==================================================================
  // 👇 HIER DEIN PROGRAMM DEFINIEREN
  //
 PROGRAM(
    HOME(AXIS_Z),
    WAIT_AXIS(AXIS_Z),
    SERVO(0, 500),
    ABS(AXIS_Z, 200, 300),
    WAIT_AXIS(AXIS_Z),
    SERVO(1, 500),
    WAIT_MS(1000),
    VIBRATE(AXIS_Z, 1, 40),
    SERVO(0, 0),
    WAIT_MS(2000),
    STOP(AXIS_Z),
    END
  );
  //
  // Start: `run` Befehl per Serial
  // Auto-Start: prog_start(); hinter PROGRAM(...)
  // ==================================================================
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  handleSerialMaster();
  prog_update();
}

// =============================================================================
// SERIELLE SCHNITTSTELLE
// =============================================================================

void handleSerialMaster() {
  if (!Serial.available()) return;

  char ch = Serial.peek();
  if (ch == 't' || ch == 'T') {
    Serial.read();
    test_mode_active = !test_mode_active;
    Serial.println(test_mode_active ? F("Testmodus AN") : F("Testmodus AUS"));
    if (test_mode_active) printMasterHelp();
    return;
  }

  if (!test_mode_active) {
    String s = Serial.readStringUntil('\n'); s.trim();
    if (s.equalsIgnoreCase("scan")) scanI2CBus();
    return;
  }

  String input = Serial.readStringUntil('\n'); input.trim();
  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("h"))       { printMasterHelp(); return; }
  if (input.equalsIgnoreCase("scan"))    { scanI2CBus(); return; }
  if (input.equalsIgnoreCase("status"))  { print_status(); return; }
  if (input.equalsIgnoreCase("run")) {
    if (prog_is_running()) { Serial.println(F("Programm laeuft bereits!")); return; }
    if (program_count == 0) { Serial.println(F("Kein Programm definiert!")); return; }
    prog_start();
    return;
  }
  if (input.equalsIgnoreCase("stop"))    { prog_stop(); return; }
  if (input.equalsIgnoreCase("enAll"))   { sendStepperCommand({0, MOVE_TYPE_STOP, 0, 0}); return; }
  if (input.equalsIgnoreCase("disAll"))  { sendStepperCommand({0, MOVE_TYPE_STOP, 99, 0}); return; }

  // STP_X,ABS,...
  if (input.startsWith("STP_") || input.startsWith("stp_")) {
    int c1 = input.indexOf(','), c2 = input.indexOf(',', c1+1), c3 = input.indexOf(',', c2+1);
    if (c1 < 0 || c2 < 0 || c3 < 0) return;
    String a = input.substring(0, c1); a.toUpperCase(); a.trim();
    String t = input.substring(c1+1, c2); t.toUpperCase(); t.trim();
    int32_t p1 = input.substring(c2+1, c3).toInt();
    int16_t p2 = input.substring(c3+1).toInt();
    StepperCommand cmd = {};
    if (a == "STP_X") cmd.axis = AXIS_X; else if (a == "STP_Y") cmd.axis = AXIS_Y; else if (a == "STP_Z") cmd.axis = AXIS_Z; else return;
    if (t == "REL") cmd.move_type = MOVE_TYPE_RELATIVE; else if (t == "ABS") cmd.move_type = MOVE_TYPE_ABSOLUTE;
    else if (t == "TIMED") cmd.move_type = MOVE_TYPE_TIMED; else if (t == "VIB") cmd.move_type = MOVE_TYPE_VIBRATE;
    else if (t == "STOP") cmd.move_type = MOVE_TYPE_STOP; else if (t == "HOMING") cmd.move_type = MOVE_TYPE_HOMING; else return;
    cmd.parameter1 = p1; cmd.parameter2 = p2;
    sendStepperCommand(cmd);
    return;
  }

  // SRV_0,490
  if (input.startsWith("SRV_") || input.startsWith("srv_")) {
    int c1 = input.indexOf(',');
    if (c1 < 0) return;
    uint8_t n = input.substring(4, c1).toInt();
    uint16_t v = input.substring(c1+1).toInt();
    sendServoCommand(n, v);
    return;
  }
}

void printMasterHelp() {
  Serial.println(F("=== BEFEHLE (Testmodus) ==="));
  Serial.println(F("STP_X,ABS,4000,800   Absolut fahren"));
  Serial.println(F("STP_X,REL,1000,600   Relativ fahren"));
  Serial.println(F("STP_X,VIB,1,40       Vibrieren"));
  Serial.println(F("STP_X,HOMING,0,0     Homing"));
  Serial.println(F("SRV_0,490            Servo setzen"));
  Serial.println(F("enAll/disAll         Treiber an/aus"));
  Serial.println(F("status               Status anzeigen"));
  Serial.println(F("scan                 I2C-Scan"));
  Serial.println(F("run/stop             Programm steuern"));
}

/* ==========================================================================

   PROGRAMM-REFERENZ

   Dein Programm definierst du in setup() mit dem PROGRAM-Makro.
   Es läuft non-blocking — loop() und Serial bleiben live.

   ──────────────── BEFEHLE ────────────────

   HOME(AXIS_X)                Homing fuer Achse X starten

   ABS(AXIS_X, 5000, 800)      Absolute Position 5000 Steps, Speed 800

   REL(AXIS_Y, -300, 600)      Relativ -300 Steps (rueckwaerts), Speed 600

   TIMED(AXIS_Z, 3000, -400)   3 Sekunden rueckwaerts mit Speed -400

   VIBRATE(AXIS_Z, 1, 40)      Vibrieren: 1 Step Amplitude, 40 Hz

   STOP(AXIS_X)                Achse X stoppen

   SERVO(0, 490)               Servo 0 auf Wert 490 (0-1000 Bereich)

   WAIT_MS(2000)               2 Sekunden warten (non-blocking!)

   WAIT_AXIS(AXIS_X)           Warten, bis Achse X ihre Fahrt beendet hat

   WAIT_ALL                    Warten, bis alle Achsen fertig sind

   ENABLE / DISABLE            Motortreiber ein-/ausschalten

   STATUS                      System-Status ausgeben

   END                         Programm-Ende (unbedingt angeben!)

   ──────────────── BEISPIEL ────────────────

   PROGRAM(
     HOME(AXIS_X),
     WAIT_AXIS(AXIS_X),
     ABS(AXIS_X,  4000, 800),
     WAIT_AXIS(AXIS_X),
     TIMED(AXIS_Z, 2000, 300),
     SERVO(0, 490),
     WAIT_MS(1500),
     SERVO(1, 150),
     WAIT_MS(1000),
     SERVO(1, 490),
     WAIT_MS(1000),
     SERVO(0, 150),
     STOP(AXIS_Z),
     ABS(AXIS_X, 0, 800),
     WAIT_AXIS(AXIS_X),
     STATUS,
     END
   );

   ──────────────── STEUERUNG ────────────────

   prog_start()           Programm von Schritt 0 starten
   prog_stop()            Programm abbrechen, Achsen stoppen
   prog_is_running()      Prueft, ob Programm laeuft

   run  (Serial)          = prog_start()
   stop (Serial)          = prog_stop()

   ──────────────── AUTO-START ────────────────

   PROGRAM(...)                      // Nur definieren
   prog_start();                     // Start bei Boot

   Oder als Funktion aufrufbar:

     void meineSequenz() {
       static const ProgramStep s[] = {
         HOME(AXIS_X), WAIT_AXIS(AXIS_X),
         ABS(AXIS_X, 5000, 800), WAIT_AXIS(AXIS_X),
         END
       };
       run_program(s, sizeof(s)/sizeof(s[0]));
     }

     // Aufruf:
     // meineSequenz();     // Startet sofort
     // run (Serial)        // Startet das PROGRAM(...) aus setup()

   ========================================================================== */
