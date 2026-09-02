#include "program.h"
#include "machine_api.h"
#include <Preferences.h>

#define PROG_MAGIC     0x48505247   // 'HPRG'
#define PROG_VERSION   1

// NVS-Schreibvorgaenge nutzen den Flash ab, deshalb wird erst gespeichert,
// wenn eine Weile nichts mehr veraendert wurde.
#define AUTOSAVE_DELAY_MS 8000

// Nach dem Absetzen eines Fahrbefehls vergeht etwas Zeit, bis der Uno die
// Achse als "busy" meldet. Ohne diese Sperre koennte ein Block sofort als
// fertig gelten und die Fahrt uebersprungen werden.
#define BUSY_GUARD_MS 150

Program gPrograms[PROG_MAX_COUNT];

static Preferences   prefs;
static bool          storageOk  = false;
static bool          dirty      = false;
static unsigned long dirtySince = 0;

// ----------------------------------------------------------------------------
// Hilfen zum Aufbau der Werkseinstellungen
// ----------------------------------------------------------------------------

static void addBlock(Program& pr, uint8_t type, uint8_t idx,
                     int32_t v1, int32_t v2, uint8_t flags = 0) {
  if (pr.blockCount >= PROG_MAX_BLOCKS) return;
  Block& b = pr.blocks[pr.blockCount++];
  b.type = type; b.idx = idx; b.flags = flags; b.pad = 0;
  b.v1 = v1; b.v2 = v2;
}

// Servo-Grundstellung, in allen drei Programmen identisch
static void addServoInit(Program& pr) {
  addBlock(pr, BLK_SERVO, 0, 1000, 0);
  addBlock(pr, BLK_SERVO, 1,  100, 0);
  addBlock(pr, BLK_SERVO, 2,    0, 0);
  addBlock(pr, BLK_SERVO, 3,  800, 0);
  addBlock(pr, BLK_SERVO, 4,  500, 0);
}

static void beginProgram(Program& pr, const char* name) {
  memset(&pr, 0, sizeof(Program));
  pr.magic       = PROG_MAGIC;
  pr.version     = PROG_VERSION;
  pr.used        = 1;
  pr.blockCount  = 0;
  pr.defaultRuns = 25;
  strncpy(pr.name, name, PROG_NAME_LEN - 1);
}

// ----------------------------------------------------------------------------
// Werkseinstellungen
// ----------------------------------------------------------------------------
// Bilden die frueheren Zustandsmaschinen P1, P2 und P3 exakt nach.
//
// Eine Besonderheit bei Programm 1: dort liefen die beiden Wartezeiten der
// Vereinzelung nicht nacheinander, sondern beide ab dem Start der Vibration
// (1500 ms und 18000 ms ab demselben Zeitpunkt). Als aufeinanderfolgende
// Bloecke sind das 1500 ms und danach 16500 ms - die Gesamtdauer von 18000 ms
// bleibt damit gleich.

static void defaultsProgram1(Program& pr) {
  beginProgram(pr, "Programm 1");
  addBlock(pr, BLK_HOME,      AXIS_Z, 0, 0);
  addServoInit(pr);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Z, 100, 100);
  addBlock(pr, BLK_VIBRATE,   AXIS_Z, 1, 50);
  addBlock(pr, BLK_SERVO,     1, 1000, 0);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_SERVO,     0, 100, 0);
  addBlock(pr, BLK_WAIT,      0, 16500, 0);
  addBlock(pr, BLK_STOP_AXIS, AXIS_Z, 0, 0);
  addBlock(pr, BLK_SERVO,     2, 800, 0);
  addBlock(pr, BLK_SERVO,     3, 0, 0);
  addBlock(pr, BLK_SERVO,     4, 0, 0);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Y,  2000, 800);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Y, -2000, 800);
}

static void defaultsProgram2(Program& pr) {
  beginProgram(pr, "Programm 2");
  addBlock(pr, BLK_HOME,      AXIS_Z, 0, 0);
  addBlock(pr, BLK_HOME,      AXIS_Y, 0, 0);
  addBlock(pr, BLK_MOVE_ABS,  AXIS_Y, -7500, 2000);
  addServoInit(pr);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Z, 100, 100);
  addBlock(pr, BLK_VIBRATE,   AXIS_Z, 1, 50);
  addBlock(pr, BLK_WAIT,      0, 5000, 0);
  addBlock(pr, BLK_SERVO,     1, 1000, 0);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_SERVO,     0, 100, 0);
  addBlock(pr, BLK_WAIT,      0, 10000, 0);
  addBlock(pr, BLK_STOP_AXIS, AXIS_Z, 0, 0);
  addBlock(pr, BLK_SERVO,     2, 300, 0);
  addBlock(pr, BLK_SERVO,     3, 500, 0);
  addBlock(pr, BLK_SERVO,     4, 60, 0);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Y, 7000, 2000);
  addBlock(pr, BLK_WAIT,      0, 12000, 0);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Y, -7000, 2000);
}

static void defaultsProgram3(Program& pr) {
  beginProgram(pr, "Programm 3");
  // Referenzfahrten nur im ersten Durchlauf, wie bisher
  addBlock(pr, BLK_HOME,      AXIS_Z, 0, 0, BLK_FLAG_FIRST_ONLY);
  addBlock(pr, BLK_HOME,      AXIS_Y, 0, 0, BLK_FLAG_FIRST_ONLY);
  addBlock(pr, BLK_MOVE_ABS,  AXIS_Y, -7500, 2000, BLK_FLAG_FIRST_ONLY);
  addServoInit(pr);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_MOVE_ABS,  AXIS_Z, 100, 100);
  addBlock(pr, BLK_VIBRATE,   AXIS_Z, 1, 50);
  addBlock(pr, BLK_WAIT,      0, 5000, 0);
  addBlock(pr, BLK_SERVO,     1, 1050, 0);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_SERVO,     0, 100, 0);
  addBlock(pr, BLK_WAIT,      0, 10000, 0);
  addBlock(pr, BLK_STOP_AXIS, AXIS_Z, 0, 0);
  // Greiferwerte haengen von der Hairpin-Laenge ab:
  // S2: 580 bei 62 mm, 550 bei 64 mm, 430 bei 69/70 mm
  // S3: 350 bei 62/64 mm, 380 bei 69 mm, 370 bei 70 mm
  addBlock(pr, BLK_SERVO,     2, 430, 0);
  addBlock(pr, BLK_SERVO,     3, 380, 0);
  addBlock(pr, BLK_SERVO,     4, 60, 0);
  addBlock(pr, BLK_WAIT,      0, 1500, 0);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Y, 7400, 2000);
  addBlock(pr, BLK_WAIT,      0, 4000, 0);
  addBlock(pr, BLK_SERVO,     2, 0, 0);
  addBlock(pr, BLK_SERVO,     3, 800, 0);
  addBlock(pr, BLK_SERVO,     4, 500, 0);
  addBlock(pr, BLK_WAIT,      0, 4000, 0);
  addBlock(pr, BLK_MOVE_REL,  AXIS_Y, -7400, 2000);
}

void program_resetAll() {
  memset(gPrograms, 0, sizeof(gPrograms));
  defaultsProgram1(gPrograms[0]);
  defaultsProgram2(gPrograms[1]);
  defaultsProgram3(gPrograms[2]);
  program_markDirty();
}

// ----------------------------------------------------------------------------
// Speicherung
// ----------------------------------------------------------------------------

static void keyFor(uint8_t i, char* out) { snprintf(out, 6, "pg%u", (unsigned)i); }

void program_begin() {
  storageOk = prefs.begin("hairpin", false);
  if (!storageOk) {
    Serial.println(F("[PRG] NVS nicht verfuegbar - arbeite nur im RAM."));
    program_resetAll();
    dirty = false;
    return;
  }

  uint8_t loaded = 0;
  for (uint8_t i = 0; i < PROG_MAX_COUNT; i++) {
    char key[6]; keyFor(i, key);
    Program tmp;
    size_t n = prefs.getBytes(key, &tmp, sizeof(Program));
    if (n == sizeof(Program) && tmp.magic == PROG_MAGIC
                             && tmp.version == PROG_VERSION
                             && tmp.blockCount <= PROG_MAX_BLOCKS) {
      gPrograms[i] = tmp;
      if (tmp.used) loaded++;
    } else {
      memset(&gPrograms[i], 0, sizeof(Program));
    }
  }

  if (loaded == 0) {
    Serial.println(F("[PRG] Keine gespeicherten Programme - Werkseinstellung."));
    program_resetAll();
    dirty = false;
  } else {
    Serial.print(F("[PRG] ")); Serial.print(loaded);
    Serial.println(F(" Programm(e) aus dem NVS geladen."));
  }
}

void program_markDirty() { dirty = true; dirtySince = millis(); }
bool program_isDirty()   { return dirty; }

void program_save() {
  if (!storageOk) {
    Serial.println(F("[PRG] Kein NVS - nicht gespeichert."));
    dirty = false;
    return;
  }
  for (uint8_t i = 0; i < PROG_MAX_COUNT; i++) {
    char key[6]; keyFor(i, key);
    gPrograms[i].magic   = PROG_MAGIC;
    gPrograms[i].version = PROG_VERSION;
    prefs.putBytes(key, &gPrograms[i], sizeof(Program));
  }
  dirty = false;
  Serial.println(F("[PRG] Programme gespeichert."));
}

void program_tick() {
  if (!dirty) return;
  if (millis() - dirtySince < AUTOSAVE_DELAY_MS) return;
  program_save();
}

// ----------------------------------------------------------------------------
// Verwaltung
// ----------------------------------------------------------------------------

uint8_t program_count() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < PROG_MAX_COUNT; i++) if (gPrograms[i].used) n++;
  return n;
}

uint8_t program_firstFree() {
  for (uint8_t i = 0; i < PROG_MAX_COUNT; i++) if (!gPrograms[i].used) return i;
  return 0xFF;
}

int program_copy(uint8_t src) {
  if (src >= PROG_MAX_COUNT || !gPrograms[src].used) return -1;
  uint8_t dst = program_firstFree();
  if (dst == 0xFF) {
    Serial.println(F("[PRG] Kein Platz fuer ein weiteres Programm."));
    return -1;
  }
  gPrograms[dst] = gPrograms[src];
  snprintf(gPrograms[dst].name, PROG_NAME_LEN, "Programm %u", (unsigned)(dst + 1));
  program_markDirty();
  Serial.print(F("[PRG] ")); Serial.print(gPrograms[src].name);
  Serial.print(F(" kopiert nach ")); Serial.println(gPrograms[dst].name);
  return dst;
}

bool program_remove(uint8_t idx) {
  if (idx >= PROG_MAX_COUNT || !gPrograms[idx].used) return false;
  if (program_isRunning() && program_runningIndex() == idx) {
    Serial.println(F("[PRG] Laeuft gerade - nicht geloescht."));
    return false;
  }
  memset(&gPrograms[idx], 0, sizeof(Program));
  program_markDirty();
  return true;
}

// ----------------------------------------------------------------------------
// Ablaufsteuerung
// ----------------------------------------------------------------------------

static bool          runActive    = false;
static uint8_t       runIdx       = 0;
static uint8_t       runBlock     = 0;
static int           runRemaining = 0;
static bool          firstRun     = true;
static bool          blockEntered = false;
static unsigned long blockStart   = 0;

void program_start(uint8_t idx, int runs) {
  if (idx >= PROG_MAX_COUNT || !gPrograms[idx].used) {
    Serial.println(F("[PRG] Programm existiert nicht."));
    return;
  }
  if (gPrograms[idx].blockCount == 0) {
    Serial.println(F("[PRG] Programm hat keine Bloecke."));
    return;
  }
  runIdx       = idx;
  runRemaining = (runs < 1) ? 1 : runs;
  runBlock     = 0;
  firstRun     = true;
  blockEntered = false;
  runActive    = true;

  Serial.print(F("[PRG] ")); Serial.print(gPrograms[idx].name);
  Serial.print(F(" gestartet (")); Serial.print(runRemaining);
  Serial.println(F(" Durchlauf(e))."));
}

void program_abort() {
  runActive = false;
  axis_stop(AXIS_X);
  axis_stop(AXIS_Y);
  axis_stop(AXIS_Z);
  Serial.println(F("[PRG] Abgebrochen, alle Achsen gestoppt."));
}

bool    program_isRunning()      { return runActive; }
uint8_t program_runningIndex()   { return runIdx; }
int     program_remainingRuns()  { return runRemaining; }
uint8_t program_currentBlock()   { return runBlock; }

static void executeBlock(const Block& b) {
  switch (b.type) {
    case BLK_HOME:      axis_home(b.idx); break;
    case BLK_MOVE_ABS:  axis_abs(b.idx, b.v1, (int16_t)b.v2); break;
    case BLK_MOVE_REL:  axis_rel(b.idx, b.v1, (int16_t)b.v2); break;
    case BLK_VIBRATE:   axis_vibrate(b.idx, b.v1, (int16_t)b.v2); break;
    case BLK_STOP_AXIS: axis_stop(b.idx); break;
    case BLK_SERVO:     servo_set(b.idx, (uint16_t)b.v1); break;
    case BLK_WAIT:      break;   // wartet nur
    default:            break;
  }
}

// Ist der Block abgearbeitet?
static bool blockFinished(const Block& b, unsigned long now) {
  switch (b.type) {
    case BLK_HOME:
    case BLK_MOVE_ABS:
    case BLK_MOVE_REL:
      // Erst nach der Sperrzeit pruefen, sonst gilt eine gerade erst
      // abgesetzte Fahrt sofort als fertig.
      if (now - blockStart < BUSY_GUARD_MS) return false;
      return !is_axis_busy(b.idx);

    case BLK_WAIT:
      return (now - blockStart) >= (unsigned long)b.v1;

    default:
      return true;   // Vibration, Stopp und Servo wirken sofort
  }
}

void program_update() {
  if (!runActive) return;

  Program& pr = gPrograms[runIdx];
  unsigned long now = millis();

  // Durchlauf zu Ende?
  if (runBlock >= pr.blockCount) {
    runRemaining--;
    if (runRemaining > 0) {
      firstRun     = false;
      runBlock     = 0;
      blockEntered = false;
      Serial.print(F("[PRG] Durchlauf fertig, noch "));
      Serial.println(runRemaining);
    } else {
      runActive = false;
      Serial.print(F("[PRG] ")); Serial.print(pr.name);
      Serial.println(F(" beendet."));
    }
    return;
  }

  const Block& b = pr.blocks[runBlock];

  // Bloecke, die nur im ersten Durchlauf gelten, spaeter ueberspringen
  if ((b.flags & BLK_FLAG_FIRST_ONLY) && !firstRun) {
    runBlock++;
    blockEntered = false;
    return;
  }

  if (!blockEntered) {
    char txt[32];
    block_describe(b, txt, sizeof(txt));
    Serial.print(F("[PRG] Block "));
    Serial.print(runBlock + 1); Serial.print('/'); Serial.print(pr.blockCount);
    Serial.print(F(": ")); Serial.println(txt);

    executeBlock(b);
    blockStart   = now;
    blockEntered = true;
    return;
  }

  if (blockFinished(b, now)) {
    runBlock++;
    blockEntered = false;
  }
}

// ----------------------------------------------------------------------------
// Anzeige
// ----------------------------------------------------------------------------

static const char* AXIS_NAME[3] = { "X", "Y", "Z" };

const char* block_typeName(uint8_t type) {
  switch (type) {
    case BLK_HOME:      return "Referenzfahrt";
    case BLK_MOVE_ABS:  return "Fahren absolut";
    case BLK_MOVE_REL:  return "Fahren relativ";
    case BLK_VIBRATE:   return "Vibration";
    case BLK_STOP_AXIS: return "Achse stoppen";
    case BLK_SERVO:     return "Servo setzen";
    case BLK_WAIT:      return "Warten";
    default:            return "?";
  }
}

// Kurzbeschreibung fuer die Ablaufliste, z.B. "Y rel 7400" oder "Warten 1500ms"
void block_describe(const Block& b, char* out, size_t n) {
  const char* ax = (b.idx < 3) ? AXIS_NAME[b.idx] : "?";
  switch (b.type) {
    case BLK_HOME:      snprintf(out, n, "%s Referenz", ax); break;
    case BLK_MOVE_ABS:  snprintf(out, n, "%s abs %ld", ax, (long)b.v1); break;
    case BLK_MOVE_REL:  snprintf(out, n, "%s rel %ld", ax, (long)b.v1); break;
    case BLK_VIBRATE:   snprintf(out, n, "%s Vib %ldHz", ax, (long)b.v2); break;
    case BLK_STOP_AXIS: snprintf(out, n, "%s Stopp", ax); break;
    case BLK_SERVO:     snprintf(out, n, "Servo %u = %ld",
                                 (unsigned)b.idx, (long)b.v1); break;
    case BLK_WAIT:      snprintf(out, n, "Warten %ldms", (long)b.v1); break;
    default:            snprintf(out, n, "?"); break;
  }
}
