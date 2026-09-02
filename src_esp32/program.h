#ifndef PROGRAM_H
#define PROGRAM_H

#include <Arduino.h>

// ============================================================================
// Programme als Datenstruktur
// ----------------------------------------------------------------------------
// Ein Programm ist eine Liste von Bloecken. Frueher war jeder Ablauf eine fest
// verdrahtete Zustandsmaschine im Quelltext; neue Programme liessen sich nur
// durch Programmieren und Flashen anlegen.
//
// Jetzt ist ein Ablauf reine Daten: er laesst sich am Display ansehen,
// Wert fuer Wert einstellen, kopieren und im NVS speichern. Genau das
// braucht man, wenn pro Hairpin-Laenge ein eigenes Programm noetig ist.
// ============================================================================

#define PROG_MAX_BLOCKS   40
#define PROG_MAX_COUNT     8
#define PROG_NAME_LEN     16

enum BlockType : uint8_t {
  BLK_HOME = 0,   // idx = Achse
  BLK_MOVE_ABS,   // idx = Achse,     v1 = Position,  v2 = Geschwindigkeit
  BLK_MOVE_REL,   // idx = Achse,     v1 = Schritte,  v2 = Geschwindigkeit
  BLK_VIBRATE,    // idx = Achse,     v1 = Amplitude, v2 = Frequenz in Hz
  BLK_STOP_AXIS,  // idx = Achse
  BLK_SERVO,      // idx = Servo 0-5, v1 = Stellwert
  BLK_WAIT,       // v1 = Millisekunden
  BLK_TYPE_COUNT
};

// Block nur im ersten Durchlauf ausfuehren (z.B. Referenzfahrten)
#define BLK_FLAG_FIRST_ONLY  0x01

struct Block {
  uint8_t type;
  uint8_t idx;      // Achse 0-2 bzw. Servo-Nummer 0-5
  uint8_t flags;
  uint8_t pad;      // haelt die Struktur auf 4-Byte-Grenzen
  int32_t v1;
  int32_t v2;
};

struct Program {
  uint32_t magic;
  uint16_t version;
  uint8_t  used;              // 0 = Platz frei
  uint8_t  blockCount;
  int32_t  defaultRuns;
  char     name[PROG_NAME_LEN];
  Block    blocks[PROG_MAX_BLOCKS];
};

extern Program gPrograms[PROG_MAX_COUNT];

// --- Verwaltung ---
void    program_begin();                  // laedt aus NVS, sonst Werkseinstellung
uint8_t program_count();                  // Anzahl belegter Plaetze
uint8_t program_firstFree();              // 0xFF, wenn kein Platz mehr frei
int     program_copy(uint8_t src);        // legt eine Kopie an, gibt Index zurueck (-1 = voll)
bool    program_remove(uint8_t idx);      // loescht ein Programm
void    program_resetAll();               // alle auf Werkseinstellung

// --- Speicherung ---
void program_markDirty();
bool program_isDirty();
void program_save();
void program_tick();                      // verzoegertes Speichern, in loop() aufrufen

// --- Ablaufsteuerung ---
void    program_start(uint8_t idx, int runs);
void    program_update();                 // in loop() aufrufen
void    program_abort();
bool    program_isRunning();
uint8_t program_runningIndex();
int     program_remainingRuns();
uint8_t program_currentBlock();

// --- Anzeige ---
const char* block_typeName(uint8_t type);
void        block_describe(const Block& b, char* out, size_t n);

#endif // PROGRAM_H
