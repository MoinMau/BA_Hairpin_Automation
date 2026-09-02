#include "params.h"
#include <Preferences.h>

// Werte muessen erhoeht werden, sobald sich die Struktur aendert. Passt die
// Version nicht, werden die gespeicherten Daten verworfen und die
// Werkseinstellungen geladen - besser als Felder falsch zuzuordnen.
#define PARAMS_MAGIC    0x48504152   // 'HPAR'
#define PARAMS_VERSION  1

// NVS-Schreibvorgaenge nutzen den Flash ab. Deshalb wird nicht bei jedem
// Tastendruck geschrieben, sondern erst, wenn eine Weile nichts mehr
// veraendert wurde.
#define AUTOSAVE_DELAY_MS 8000

Params gParams[3];

static Preferences    prefs;
static bool           storageOk  = false;
static bool           dirty      = false;
static unsigned long  dirtySince = 0;

static const char* KEYS[3] = { "p1", "p2", "p3" };

// ----------------------------------------------------------------------------
// Werkseinstellungen
// ----------------------------------------------------------------------------
// Entsprechen exakt den Werten, die vor der Umstellung fest in
// updateSequence() standen.

static void loadDefaults(uint8_t i) {
  Params& p = gParams[i];
  p.magic   = PARAMS_MAGIC;
  p.version = PARAMS_VERSION;

  // Fuer alle drei Programme gleich
  p.servoInit0    = 1000;
  p.servoInit1    = 100;
  p.servoInit2    = 0;
  p.servoInit3    = 800;
  p.servoInit4    = 500;
  p.servoSettleMs = 1500;

  p.zMoveSpeed    = 100;
  p.zMovePos      = 100;
  p.vibAmplitude  = 1;
  p.vibFreqHz     = 50;
  p.feed2Servo0   = 100;

  p.yStartPos     = -7500;
  p.yStartSpeed   = 2000;

  p.openServo2    = 0;
  p.openServo3    = 800;
  p.openServo4    = 500;
  p.openWaitMs    = 4000;

  switch (i) {
    case 0:  // Programm 1
      p.slideWaitMs    = 0;
      p.feed1Servo1    = 1000;
      p.feedDurationMs = 18000;
      p.gripServo2     = 800;
      p.gripServo3     = 0;
      p.gripServo4     = 0;
      p.yFeedSteps     = 2000;
      p.yFeedSpeed     = 800;
      p.robotWaitMs    = 0;
      break;

    case 1:  // Programm 2
      p.slideWaitMs    = 5000;
      p.feed1Servo1    = 1000;
      p.feedDurationMs = 10000;
      p.gripServo2     = 300;
      p.gripServo3     = 500;
      p.gripServo4     = 60;
      p.yFeedSteps     = 7000;
      p.yFeedSpeed     = 2000;
      p.robotWaitMs    = 12000;
      break;

    default:  // Programm 3
      p.slideWaitMs    = 5000;
      p.feed1Servo1    = 1050;
      p.feedDurationMs = 10000;
      // Greiferwerte haengen von der Hairpin-Laenge ab:
      // S2: 550 bei 64 mm, 430 bei 69/70 mm, 580 bei 62 mm
      // S3: 350 bei 64 mm, 380 bei 69 mm, 370 bei 70 mm, 350 bei 62 mm
      p.gripServo2     = 430;
      p.gripServo3     = 380;
      p.gripServo4     = 60;
      p.yFeedSteps     = 7400;
      p.yFeedSpeed     = 2000;
      p.robotWaitMs    = 4000;
      break;
  }
}

// ----------------------------------------------------------------------------

void params_begin() {
  storageOk = prefs.begin("hairpin", false);
  if (!storageOk) {
    Serial.println(F("[PAR] NVS nicht verfuegbar - arbeite nur im RAM."));
    for (uint8_t i = 0; i < 3; i++) loadDefaults(i);
    return;
  }

  for (uint8_t i = 0; i < 3; i++) {
    Params tmp;
    size_t n = prefs.getBytes(KEYS[i], &tmp, sizeof(Params));

    if (n == sizeof(Params) && tmp.magic == PARAMS_MAGIC
                            && tmp.version == PARAMS_VERSION) {
      gParams[i] = tmp;
      Serial.print(F("[PAR] Programm ")); Serial.print(i + 1);
      Serial.println(F(": gespeicherte Werte geladen."));
    } else {
      loadDefaults(i);
      Serial.print(F("[PAR] Programm ")); Serial.print(i + 1);
      Serial.println(F(": Werkseinstellungen."));
    }
  }
}

void params_markDirty() {
  dirty      = true;
  dirtySince = millis();
}

bool params_isDirty() { return dirty; }

void params_save() {
  if (!storageOk) {
    Serial.println(F("[PAR] Kein NVS - nicht gespeichert."));
    dirty = false;
    return;
  }
  for (uint8_t i = 0; i < 3; i++) {
    gParams[i].magic   = PARAMS_MAGIC;
    gParams[i].version = PARAMS_VERSION;
    prefs.putBytes(KEYS[i], &gParams[i], sizeof(Params));
  }
  dirty = false;
  Serial.println(F("[PAR] Werte gespeichert."));
}

void params_reset(uint8_t idx) {
  if (idx > 2) return;
  loadDefaults(idx);
  params_markDirty();
  Serial.print(F("[PAR] Programm ")); Serial.print(idx + 1);
  Serial.println(F(": auf Werkseinstellungen zurueckgesetzt."));
}

void params_tick() {
  if (!dirty) return;
  if (millis() - dirtySince < AUTOSAVE_DELAY_MS) return;
  params_save();
}
