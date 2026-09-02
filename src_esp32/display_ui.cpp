#include "display_ui.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#include "config_display.h"
#include "buttons.h"
#include "machine_api.h"
#include "program.h"
#include "config_nano.h"   // Servo-Limits: die Grenzen, die der Nano wirklich zulaesst

// ----------------------------------------------------------------------------
// Display-Objekt (Software- oder Hardware-SPI, siehe config_display.h)
// ----------------------------------------------------------------------------
#if TFT_USE_SOFT_SPI
static Adafruit_ST7735 tft(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_MOSI,
                           TFT_PIN_SCLK, TFT_PIN_RST);
#else
static Adafruit_ST7735 tft(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);
#endif

// --- Farbschema ---
#define COL_BG         ST77XX_BLACK
#define COL_TEXT       ST77XX_WHITE
#define COL_DIM        0x8410
#define COL_HEADER_BG  0x001F
#define COL_SEL_BG     0xFD20
#define COL_SEL_TEXT   ST77XX_BLACK
#define COL_VALUE      0x07FF
#define COL_OK         ST77XX_GREEN
#define COL_ALARM      ST77XX_RED

// --- Layout (Querformat 160x128) ---
static int16_t scrW = 160;
static int16_t scrH = 128;
#define HEADER_H   14
#define FOOTER_H   12
#define ROW_H      14
#define PAD_X       5
#define CHAR_W      6

// ============================================================================
// MENUE-MODELL
// ----------------------------------------------------------------------------
// Feste Seiten stecken in Tabellen. Seiten, deren Inhalt erst zur Laufzeit
// feststeht - die Programmliste, der Ablauf eines Programms, die Felder eines
// Blocks - werden beim Oeffnen in einen Puffer gebaut. Der Renderer sieht
// keinen Unterschied.
// ============================================================================

enum RowType {
  ROW_SUBMENU,     // ENTER/RIGHT oeffnet eine Unterseite
  ROW_ACTION,      // ENTER loest eine Aktion aus
  ROW_VALUE,       // LEFT/RIGHT aendert eine feste Variable
  ROW_CHOICE,      // LEFT/RIGHT waehlt aus einer Liste
  ROW_JOG,         // LEFT/RIGHT verfaehrt die Achse
  ROW_PROGFIELD,   // Feld im gewaehlten Programm
  ROW_BLOCKFIELD,  // Feld im gewaehlten Block
  ROW_INFO,        // nur Anzeige
  ROW_GROUP,       // Ueberschrift einer Funktionsgruppe, nicht anwaehlbar
  ROW_BACK
};

// Felder eines Blocks. Block mischt uint8_t und int32_t, deshalb wird ueber
// eine Feldkennung zugegriffen statt ueber einen Byte-Offset.
enum BlockField { BF_IDX = 0, BF_V1, BF_V2, BF_FLAG };

enum PageId {
  PAGE_MAIN = 0,
  PAGE_PROGRAMS,    // dynamisch: Liste der Programme
  PAGE_PROGRAM,
  PAGE_FLOW,        // dynamisch: Bloecke des Programms
  PAGE_BLOCK,       // dynamisch: Felder eines Blocks
  PAGE_STEPPERS,
  PAGE_AXIS,
  PAGE_SERVOS,
  PAGE_SETTINGS,
  PAGE_COUNT
};

enum ActionId {
  ACT_NONE = 0,
  ACT_OPEN_PROG, ACT_NEW_PROG, ACT_START, ACT_RENAME, ACT_DELETE, ACT_SAVE,
  ACT_OPEN_BLOCK,
  ACT_AXIS_X, ACT_AXIS_Y, ACT_AXIS_Z,
  ACT_HOME_ALL, ACT_DRV_ON, ACT_DRV_OFF,
  ACT_AXIS_HOME, ACT_AXIS_STOP,
  ACT_SERVO_APPLY, ACT_SERVO_INIT,
  ACT_HELP, ACT_FACTORY,
  ACT_HALT
};

struct MenuRow {
  const char*     label;
  RowType         type;
  uint8_t         action;      // ACT_* bzw. PAGE_* bei ROW_SUBMENU
  uint8_t         arg;         // Servo-Nr., Block-Index, Feldkennung
  int32_t*        value;       // ROW_VALUE / ROW_CHOICE
  int32_t         vmin, vmax, vstep;
  const int32_t*  choices;
  uint8_t         choiceCount;
};

struct MenuPage {
  const char*    title;        // NULL -> dynamischer Titel
  const MenuRow* rows;         // NULL -> Seite wird zur Laufzeit gebaut
  uint8_t        count;
};

#define R_SUB(lbl, page)              { lbl, ROW_SUBMENU, page, 0, nullptr, 0,0,0, nullptr, 0 }
#define R_ACT(lbl, act)               { lbl, ROW_ACTION,  act,  0, nullptr, 0,0,0, nullptr, 0 }
#define R_VAL(lbl, var, lo, hi, st)   { lbl, ROW_VALUE, ACT_NONE, 0, &(var), lo, hi, st, nullptr, 0 }
#define R_VALA(lbl, var, lo, hi, st, act, a) { lbl, ROW_VALUE, act, a, &(var), lo, hi, st, nullptr, 0 }
#define R_CHO(lbl, var, arr)          { lbl, ROW_CHOICE, ACT_NONE, 0, &(var), 0, (int32_t)(sizeof(arr)/sizeof(arr[0]))-1, 1, arr, sizeof(arr)/sizeof(arr[0]) }
#define R_JOG(lbl)                    { lbl, ROW_JOG, ACT_NONE, 0, nullptr, 0,0,0, nullptr, 0 }
#define R_BACK()                      { "Zurueck", ROW_BACK, ACT_NONE, 0, nullptr, 0,0,0, nullptr, 0 }
#define R_PROG(lbl, lo, hi, st)       { lbl, ROW_PROGFIELD, ACT_NONE, 0, nullptr, lo, hi, st, nullptr, 0 }
#define R_BLK(lbl, field, lo, hi, st) { lbl, ROW_BLOCKFIELD, ACT_NONE, field, nullptr, lo, hi, st, nullptr, 0 }
#define R_INFO(lbl)                   { lbl, ROW_INFO, ACT_NONE, 0, nullptr, 0,0,0, nullptr, 0 }
#define R_GROUP(lbl)                  { lbl, ROW_GROUP, ACT_NONE, 0, nullptr, 0,0,0, nullptr, 0 }

// ============================================================================
// FESTE SEITEN
// ============================================================================

static int32_t jogStepIdx = 2;
static int32_t jogSpeed   = 800;
static int32_t servoVal[6] = { 500, 500, 500, 500, 500, 500 };
static const int32_t JOG_STEPS[] = { 1, 10, 100, 1000 };

static const MenuRow ROWS_MAIN[] = {
  R_SUB("Programme",      PAGE_PROGRAMS),
  R_SUB("Schrittmotoren", PAGE_STEPPERS),
  R_SUB("Servomotoren",   PAGE_SERVOS),
  R_SUB("Einstellungen",  PAGE_SETTINGS),
  R_ACT("NOT-HALT",       ACT_HALT)
};

static const MenuRow ROWS_PROGRAM[] = {
  R_PROG("Durchlaeufe",     1, 999, 1),
  R_ACT ("START",           ACT_START),
  R_SUB ("Ablauf + Werte",  PAGE_FLOW),
  R_ACT ("Umbenennen",      ACT_RENAME),
  R_ACT ("Speichern",       ACT_SAVE),
  R_ACT ("Loeschen",        ACT_DELETE),
  R_BACK()
};

static const MenuRow ROWS_STEPPERS[] = {
  R_ACT("Achse X",            ACT_AXIS_X),
  R_ACT("Achse Y",            ACT_AXIS_Y),
  R_ACT("Achse Z",            ACT_AXIS_Z),
  R_ACT("Referenzfahrt alle", ACT_HOME_ALL),
  R_ACT("Treiber EIN",        ACT_DRV_ON),
  R_ACT("Treiber AUS",        ACT_DRV_OFF),
  R_BACK()
};

static const MenuRow ROWS_AXIS[] = {
  R_ACT("Referenzfahrt",  ACT_AXIS_HOME),
  R_CHO("Schrittweite",   jogStepIdx, JOG_STEPS),
  R_VAL("Geschw. St/s",   jogSpeed, 50, 3000, 50),
  R_JOG("Fahren   - / +"),
  R_ACT("STOPP",          ACT_AXIS_STOP),
  R_BACK()
};

// Die Grenzen kommen aus config_nano.h. Der Nano begrenzt jeden Servo auf
// seinen eigenen Bereich; ein groesserer Wert im Menue haette keine Wirkung.
static const MenuRow ROWS_SERVOS[] = {
  R_VALA("Servo 0  D7",  servoVal[0], S0_MIN, S0_MAX, 10, ACT_SERVO_APPLY, 0),
  R_VALA("Servo 1  D8",  servoVal[1], S1_MIN, S1_MAX, 10, ACT_SERVO_APPLY, 1),
  R_VALA("Servo 2  D9",  servoVal[2], S2_MIN, S2_MAX, 10, ACT_SERVO_APPLY, 2),
  R_VALA("Servo 3  D10", servoVal[3], S3_MIN, S3_MAX, 10, ACT_SERVO_APPLY, 3),
  R_VALA("Servo 4  D11", servoVal[4], S4_MIN, S4_MAX, 10, ACT_SERVO_APPLY, 4),
  R_VALA("Servo 5  D12", servoVal[5], S5_MIN, S5_MAX, 10, ACT_SERVO_APPLY, 5),
  R_ACT ("Grundstellung", ACT_SERVO_INIT),
  R_BACK()
};

static const MenuRow ROWS_SETTINGS[] = {
  R_ACT("Erklaerung",         ACT_HELP),
  R_ACT("Alles speichern",    ACT_SAVE),
  R_ACT("Werkseinstellungen", ACT_FACTORY),
  R_BACK()
};

static const MenuPage PAGES[PAGE_COUNT] = {
  { "Hauptmenue",     ROWS_MAIN,     sizeof(ROWS_MAIN)     / sizeof(MenuRow) },
  { "Programme",      nullptr,       0 },   // dynamisch
  { nullptr,          ROWS_PROGRAM,  sizeof(ROWS_PROGRAM)  / sizeof(MenuRow) },
  { nullptr,          nullptr,       0 },   // dynamisch
  { nullptr,          nullptr,       0 },   // dynamisch
  { "Schrittmotoren", ROWS_STEPPERS, sizeof(ROWS_STEPPERS) / sizeof(MenuRow) },
  { nullptr,          ROWS_AXIS,     sizeof(ROWS_AXIS)     / sizeof(MenuRow) },
  { "Servomotoren",   ROWS_SERVOS,   sizeof(ROWS_SERVOS)   / sizeof(MenuRow) },
  { "Einstellungen",  ROWS_SETTINGS, sizeof(ROWS_SETTINGS) / sizeof(MenuRow) }
};

// ============================================================================
// ZUSTAND
// ============================================================================

enum UiScreen { SCR_SPLASH, SCR_PAGE, SCR_RUNNING, SCR_HALTED, SCR_CONFIRM, SCR_RENAME, SCR_HELP };

static UiScreen      currentScreen = SCR_SPLASH;
static unsigned long splashStart   = 0;
#define SPLASH_MS 2000

static uint8_t curPage   = PAGE_MAIN;
static uint8_t curRow    = 0;
static uint8_t scrollTop = 0;
static char    dynTitle[26] = "";

static uint8_t selProg  = 0;   // gewaehltes Programm
static uint8_t selBlock = 0;   // gewaehlter Block
static uint8_t curAxis  = AXIS_X;

// Puffer fuer dynamisch gebaute Seiten
#define DYN_MAX   (PROG_MAX_BLOCKS + GRP_COUNT + 4)
static MenuRow dynRows[DYN_MAX];
static uint8_t dynCount = 0;
static char    dynLabel[DYN_MAX][22];

struct NavEntry { uint8_t page; uint8_t row; uint8_t top; };
static NavEntry navStack[5];
static uint8_t  navDepth = 0;

static bool needsRedraw = true;
static bool needsRows   = false;
static bool needsHeader = false;

static StepperStatus mStatus = { 0, 0, 0, 0, 0 };
static unsigned long mStatusLast = 0;
#define STATUS_POLL_MS 300

static bool          unoOnline    = false;
static unsigned long presenceLast = 0;
#define PRESENCE_POLL_MS 2000

static unsigned long leftHoldStart = 0;
#define HALT_HOLD_MS 1500

// Leerlauf
// ----------------------------------------------------------------------------
// Nach einer Minute ohne Eingabe kehrt das Menue zum Hauptbildschirm zurueck,
// damit niemand versehentlich in einem Untermenue stehen bleibt. Nach zehn
// Minuten werden die Schrittmotortreiber stromlos geschaltet: sie werden sonst
// dauerhaft warm, ohne dass etwas passiert. Der naechste Tastendruck schaltet
// sie wieder ein.
static unsigned long lastActivity = 0;
static bool          driversOff   = false;
#define IDLE_HOME_MS    60000UL
#define IDLE_MOTORS_MS 600000UL

// Beschleunigung beim Halten einer Richtungstaste. Ohne sie waere ein Weg
// von 7400 Schritten bei Schrittweite 10 nicht in vertretbarer Zeit
// einzustellen.
static uint8_t       editRepeat = 0;
static unsigned long editLast   = 0;
static int           editDir    = 0;

// Bestaetigungsdialog
static const char* confirmText = "";
static uint8_t     confirmAct  = ACT_NONE;   // ACT_NONE = reiner Hinweis

// Namens-Editor
// Zeichensatz bewusst kurz gehalten: mit fuenf Tasten ist jedes zusaetzliche
// Zeichen ein weiterer Tastendruck beim Durchblaettern.
static const char NAME_CHARS[] =
  " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._";
#define NAME_CHAR_COUNT (sizeof(NAME_CHARS) - 1)

static char    nameBuf[PROG_NAME_LEN] = "";
static uint8_t namePos = 0;

// --- Hilfe ---
// Kurze Erklaerung des Systems, in Seiten zu je hoechstens acht Zeilen.
// Eine Zeile fasst 26 Zeichen.
struct HelpPage { const char* title; const char* lines[8]; };

static const HelpPage HELP[] = {
  { "Aufbau", {
    "Drei Steuerungen am I2C:",
    "",
    "ESP32  Ablauf und Menue",
    "Uno    Schrittmotoren",
    "       X, Y und Z",
    "Nano   Servos und",
    "       Sensoren",
    nullptr } },

  { "Programme", {
    "Ein Programm ist eine",
    "Liste von Bloecken, die",
    "nacheinander ablaufen.",
    "",
    "Ablauf + Werte zeigt sie",
    "nach Funktion gruppiert",
    "und laesst jeden Wert",
    "einstellen." } },

  { "Neues Programm", {
    "Programme > Neues",
    "Programm legt eine Kopie",
    "des Basisprogramms an.",
    "",
    "Danach umbenennen und im",
    "Ablauf die Werte an die",
    "Hairpin-Laenge anpassen.",
    nullptr } },

  { "Bedienung", {
    "UP/DOWN  Zeile waehlen",
    "LEFT     zurueck, Wert -",
    "RIGHT    oeffnen, Wert +",
    "ENTER    ausloesen",
    "",
    "Taste halten aendert",
    "Werte in groesseren",
    "Schritten." } },

  { "Ruhezustand", {
    "Nach 1 Minute ohne",
    "Eingabe springt das",
    "Menue zum Hauptbild.",
    "",
    "Nach 10 Minuten werden",
    "die Motoren stromlos.",
    "Ein Tastendruck weckt",
    "sie. Danach Referenz!" } },

  { "Sicherheit", {
    "NOT-HALT: LEFT 1,5 s",
    "halten, oder ENTER",
    "waehrend ein Programm",
    "laeuft.",
    "",
    "Er stoppt nur ueber den",
    "I2C-Bus und ersetzt",
    "keinen Nothalt-Schalter." } }
};
#define HELP_COUNT (sizeof(HELP) / sizeof(HELP[0]))
static uint8_t helpPage = 0;

// ============================================================================
// ZUGRIFF AUF ZEILENWERTE
// ============================================================================

static Block* selBlockPtr() {
  Program& pr = gPrograms[selProg];
  if (selBlock >= pr.blockCount) return nullptr;
  return &pr.blocks[selBlock];
}

static int32_t rowGet(const MenuRow& r) {
  switch (r.type) {
    case ROW_VALUE:
    case ROW_CHOICE:
      return *r.value;
    case ROW_PROGFIELD:
      return gPrograms[selProg].defaultRuns;
    case ROW_BLOCKFIELD: {
      Block* b = selBlockPtr();
      if (!b) return 0;
      switch (r.arg) {
        case BF_IDX:  return b->idx;
        case BF_V1:   return b->v1;
        case BF_V2:   return b->v2;
        case BF_FLAG: return (b->flags & BLK_FLAG_FIRST_ONLY) ? 1 : 0;
      }
      return 0;
    }
    default: return 0;
  }
}

static void rowSet(const MenuRow& r, int32_t v) {
  switch (r.type) {
    case ROW_VALUE:
    case ROW_CHOICE:
      *r.value = v;
      break;
    case ROW_PROGFIELD:
      gPrograms[selProg].defaultRuns = v;
      program_markDirty();
      break;
    case ROW_BLOCKFIELD: {
      Block* b = selBlockPtr();
      if (!b) return;
      switch (r.arg) {
        case BF_IDX:  b->idx = (uint8_t)v; break;
        case BF_V1:   b->v1  = v; break;
        case BF_V2:   b->v2  = v; break;
        case BF_FLAG:
          if (v) b->flags |=  BLK_FLAG_FIRST_ONLY;
          else   b->flags &= ~BLK_FLAG_FIRST_ONLY;
          break;
      }
      program_markDirty();
      break;
    }
    default: break;
  }
}

static bool rowIsEditable(const MenuRow& r) {
  return r.type == ROW_VALUE || r.type == ROW_CHOICE
      || r.type == ROW_PROGFIELD || r.type == ROW_BLOCKFIELD;
}

static bool rowConsumesLeftRight(const MenuRow& r) {
  return rowIsEditable(r) || r.type == ROW_JOG;
}

// ============================================================================
// AKTUELLE SEITE
// ============================================================================

static void buildDynamicPage(uint8_t page);

static const MenuRow* pageRows() {
  return PAGES[curPage].rows ? PAGES[curPage].rows : dynRows;
}
static uint8_t pageCount() {
  return PAGES[curPage].rows ? PAGES[curPage].count : dynCount;
}

// ============================================================================
// ZEICHNEN
// ============================================================================

static uint8_t visibleRows() { return (scrH - HEADER_H - FOOTER_H) / ROW_H; }

static void drawTextRight(const char* s, int16_t rightX, int16_t y, uint16_t col) {
  int16_t w = (int16_t)strlen(s) * CHAR_W;
  tft.setTextColor(col);
  tft.setCursor(rightX - w, y);
  tft.print(s);
}

static void pollStatus() {
  unsigned long now = millis();
  if (presenceLast == 0 || (now - presenceLast) >= PRESENCE_POLL_MS) {
    presenceLast = now;
    bool was = unoOnline;
    unoOnline = i2c_devicePresent(I2C_ADDR_UNO);
    if (was != unoOnline) {
      Serial.print(F("[UI] Uno (0x33): "));
      Serial.println(unoOnline ? F("online") : F("nicht erreichbar"));
    }
  }
  if (!unoOnline) { mStatus = { 0, 0, 0, 0, 0 }; return; }
  if ((now - mStatusLast) < STATUS_POLL_MS) return;
  mStatusLast = now;
  mStatus = get_stepper_status();
}

static void buildStatusText(char* out, size_t n) {
  if (currentScreen == SCR_PAGE && curPage == PAGE_AXIS && unoOnline) {
    int32_t pos = (curAxis == AXIS_X) ? mStatus.current_pos_x
                : (curAxis == AXIS_Y) ? mStatus.current_pos_y
                                      : mStatus.current_pos_z;
    bool busy = (mStatus.axis_busy & (1 << curAxis)) != 0;
    snprintf(out, n, "%ld%s", (long)pos, busy ? " >" : "");
    return;
  }
  if (program_isRunning()) { snprintf(out, n, "LAEUFT");   return; }
  if (driversOff)          { snprintf(out, n, "MOT AUS");  return; }
  if (program_isDirty())   { snprintf(out, n, "* offen");  return; }
  if (!unoOnline)          { snprintf(out, n, "KEIN I2C"); return; }
  if (mStatus.axis_busy & 0x07) {
    snprintf(out, n, "%c%c%c",
             (mStatus.axis_busy & BUSY_X) ? 'X' : '.',
             (mStatus.axis_busy & BUSY_Y) ? 'Y' : '.',
             (mStatus.axis_busy & BUSY_Z) ? 'Z' : '.');
  } else {
    snprintf(out, n, "BEREIT");
  }
}

static char lastStatusText[14] = "";

static void drawHeader(const char* title) {
  tft.fillRect(0, 0, scrW, HEADER_H, COL_HEADER_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(PAD_X, 4);
  tft.print(title);
  buildStatusText(lastStatusText, sizeof(lastStatusText));
  drawTextRight(lastStatusText, scrW - PAD_X, 4, COL_TEXT);
}

static void drawFooter(const char* hint) {
  tft.fillRect(0, scrH - FOOTER_H, scrW, FOOTER_H, COL_BG);
  tft.drawFastHLine(0, scrH - FOOTER_H, scrW, COL_DIM);
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(PAD_X, scrH - FOOTER_H + 3);
  tft.print(hint);
}

static void drawScrollMark() {
  if (pageCount() <= visibleRows()) return;
  char m[10];
  snprintf(m, sizeof(m), "%u/%u", (unsigned)(curRow + 1), (unsigned)pageCount());
  drawTextRight(m, scrW - PAD_X, scrH - FOOTER_H + 3, COL_DIM);
}

static void rowValueText(const MenuRow& r, char* out, size_t n) {
  switch (r.type) {
    case ROW_VALUE:
    case ROW_PROGFIELD:
      snprintf(out, n, "%ld", (long)rowGet(r));
      break;
    case ROW_BLOCKFIELD: {
      const Block* b = selBlockPtr();
      if (r.arg == BF_FLAG) {
        snprintf(out, n, "%s", rowGet(r) ? "ja" : "nein");
      } else if (r.arg == BF_IDX && b && b->type != BLK_SERVO) {
        snprintf(out, n, "%c", (char)('X' + rowGet(r)));
      } else if (r.arg == BF_V2 && b && b->type == BLK_SERVO && rowGet(r) == 0) {
        snprintf(out, n, "sofort");
      } else {
        snprintf(out, n, "%ld", (long)rowGet(r));
      }
      break;
    }
    case ROW_CHOICE:
      snprintf(out, n, "%ld", (long)r.choices[rowGet(r)]);
      break;
    case ROW_JOG:
      snprintf(out, n, "%ld", (long)JOG_STEPS[jogStepIdx]);
      break;
    case ROW_SUBMENU:
      snprintf(out, n, ">");
      break;
    default:
      out[0] = '\0';
      break;
  }
}

static void drawRows() {
  const MenuRow* rows = pageRows();
  uint8_t        cnt  = pageCount();
  uint8_t        vis  = visibleRows();

  if (curRow >= cnt) curRow = cnt ? cnt - 1 : 0;
  if (curRow < scrollTop)        scrollTop = curRow;
  if (curRow >= scrollTop + vis) scrollTop = curRow - vis + 1;
  if (cnt <= vis)                scrollTop = 0;

  tft.fillRect(0, HEADER_H, scrW, scrH - HEADER_H - FOOTER_H, COL_BG);
  tft.setTextSize(1);

  for (uint8_t i = 0; i < vis && (scrollTop + i) < cnt; i++) {
    uint8_t        idx = scrollTop + i;
    const MenuRow& r   = rows[idx];
    int16_t        y   = HEADER_H + i * ROW_H;
    bool           sel = (idx == curRow);

    if (sel && r.type != ROW_GROUP) tft.fillRect(0, y, scrW, ROW_H - 1, COL_SEL_BG);

    if (r.type == ROW_GROUP) {
      // Abschnittsueberschrift: eigene Farbe und Trennlinie
      tft.setTextColor(COL_SEL_BG);
      tft.setCursor(PAD_X, y + 3);
      tft.print(r.label);
      tft.drawFastHLine(PAD_X, y + ROW_H - 2, scrW - 2 * PAD_X, COL_SEL_BG);
      continue;
    }

    uint16_t col = sel ? COL_SEL_TEXT
                 : (r.type == ROW_ACTION && (r.action == ACT_HALT
                                          || r.action == ACT_DELETE))
                   ? COL_ALARM
                 : (r.type == ROW_INFO) ? COL_DIM : COL_TEXT;
    tft.setTextColor(col);
    tft.setCursor(PAD_X, y + 3);
    tft.print(r.label);

    char val[14];
    rowValueText(r, val, sizeof(val));
    if (val[0]) drawTextRight(val, scrW - PAD_X, y + 3,
                              sel ? COL_SEL_TEXT : COL_VALUE);
  }
}

static const char* footerHint() {
  const MenuRow& r = pageRows()[curRow];
  if (rowIsEditable(r))          return "L/R aendern";
  if (r.type == ROW_JOG)         return "L/R = Achse verfahren";
  if (r.type == ROW_SUBMENU)     return "ENTER oeffnen";
  if (r.type == ROW_BACK)        return "ENTER zurueck";
  if (r.type == ROW_INFO)        return "";
  return "ENTER ausloesen";
}

static void composeTitle(char* out, size_t n) {
  const MenuPage& p = PAGES[curPage];
  if (p.title) strncpy(out, p.title, n - 1);
  else         strncpy(out, dynTitle, n - 1);
  out[n - 1] = '\0';
}

static void drawPage() {
  char title[26];
  composeTitle(title, sizeof(title));
  drawHeader(title);
  drawRows();
  drawFooter(footerHint());
  drawScrollMark();
}

// ============================================================================
// DYNAMISCHE SEITEN
// ============================================================================

static void addDynRow(const MenuRow& r) {
  if (dynCount < DYN_MAX) dynRows[dynCount++] = r;
}

// --- Liste aller Programme ---
static void buildProgramList() {
  dynCount = 0;
  for (uint8_t i = 0; i < PROG_MAX_COUNT; i++) {
    if (!gPrograms[i].used) continue;
    snprintf(dynLabel[dynCount], sizeof(dynLabel[0]), "%s", gPrograms[i].name);
    MenuRow r = { dynLabel[dynCount], ROW_ACTION, ACT_OPEN_PROG, i,
                  nullptr, 0,0,0, nullptr, 0 };
    addDynRow(r);
  }
  if (program_firstFree() != 0xFF) {
    snprintf(dynLabel[dynCount], sizeof(dynLabel[0]), "Neues Programm");
    MenuRow r = { dynLabel[dynCount], ROW_ACTION, ACT_NEW_PROG, 0,
                  nullptr, 0,0,0, nullptr, 0 };
    addDynRow(r);
  }
  MenuRow back = R_BACK();
  addDynRow(back);
}

// --- Ablauf: ein Eintrag je Block, mit Kurzbeschreibung ---
static void buildFlowList() {
  Program& pr = gPrograms[selProg];
  dynCount = 0;
  uint8_t lastGroup = 0xFF;

  for (uint8_t i = 0; i < pr.blockCount; i++) {
    // Ueberschrift, sobald ein neuer Funktionsabschnitt beginnt
    if (pr.blocks[i].group != lastGroup) {
      lastGroup = pr.blocks[i].group;
      snprintf(dynLabel[dynCount], sizeof(dynLabel[0]), "%s",
               block_groupName(lastGroup));
      MenuRow g = { dynLabel[dynCount], ROW_GROUP, ACT_NONE, 0,
                    nullptr, 0,0,0, nullptr, 0 };
      addDynRow(g);
    }

    char desc[26];
    block_describe(pr.blocks[i], desc, sizeof(desc));
    // Bloecke, die nur im ersten Durchlauf laufen, mit * kennzeichnen
    snprintf(dynLabel[dynCount], sizeof(dynLabel[0]), "%2u %s%s",
             (unsigned)(i + 1), desc,
             (pr.blocks[i].flags & BLK_FLAG_FIRST_ONLY) ? " *" : "");
    MenuRow r = { dynLabel[dynCount], ROW_ACTION, ACT_OPEN_BLOCK, i,
                  nullptr, 0,0,0, nullptr, 0 };
    addDynRow(r);
  }
  MenuRow back = R_BACK();
  addDynRow(back);
  snprintf(dynTitle, sizeof(dynTitle), "%s Ablauf", pr.name);
}

// --- Felder eines Blocks, passend zum Blocktyp ---
static void buildBlockPage() {
  Block* b = selBlockPtr();
  dynCount = 0;
  if (!b) { MenuRow back = R_BACK(); addDynRow(back); return; }

  MenuRow info = R_INFO(block_typeName(b->type));
  addDynRow(info);

  switch (b->type) {
    case BLK_HOME:
    case BLK_STOP_AXIS: {
      MenuRow r = R_BLK("Achse", BF_IDX, 0, 2, 1);
      addDynRow(r);
      break;
    }
    case BLK_MOVE_ABS:
    case BLK_MOVE_REL: {
      MenuRow a = R_BLK("Achse",       BF_IDX, 0, 2, 1);
      MenuRow v = R_BLK("Weg",         BF_V1, -30000, 30000, 10);
      MenuRow s = R_BLK("Geschw St/s", BF_V2,      1,  5000, 10);
      addDynRow(a); addDynRow(v); addDynRow(s);
      break;
    }
    case BLK_VIBRATE: {
      MenuRow a = R_BLK("Achse",     BF_IDX, 0, 2, 1);
      MenuRow v = R_BLK("Amplitude", BF_V1,  0, 100, 1);
      MenuRow f = R_BLK("Freq Hz",   BF_V2,  1, 200, 1);
      addDynRow(a); addDynRow(v); addDynRow(f);
      break;
    }
    case BLK_SERVO: {
      // Grenzen des jeweiligen Servos aus der Nano-Konfiguration
      uint8_t sn = (b->idx < 6) ? b->idx : 0;
      MenuRow a = R_BLK("Servo Nr",  BF_IDX, 0, 5, 1);
      MenuRow v = R_BLK("Stellwert", BF_V1,
                        SERVO_MIN_LIMITS[sn], SERVO_MAX_LIMITS[sn], 10);
      MenuRow g = R_BLK("Geschw /s", BF_V2, 0, 2000, 10);
      addDynRow(a); addDynRow(v); addDynRow(g);
      break;
    }
    case BLK_WAIT: {
      MenuRow v = R_BLK("Zeit ms", BF_V1, 0, 120000, 100);
      addDynRow(v);
      break;
    }
  }

  MenuRow f = R_BLK("nur 1. Lauf", BF_FLAG, 0, 1, 1);
  addDynRow(f);
  MenuRow back = R_BACK();
  addDynRow(back);

  snprintf(dynTitle, sizeof(dynTitle), "%s Bl.%u",
           gPrograms[selProg].name, (unsigned)(selBlock + 1));
}

static void buildDynamicPage(uint8_t page) {
  switch (page) {
    case PAGE_PROGRAMS: buildProgramList(); break;
    case PAGE_FLOW:     buildFlowList();    break;
    case PAGE_BLOCK:    buildBlockPage();   break;
    case PAGE_PROGRAM:
      snprintf(dynTitle, sizeof(dynTitle), "%s", gPrograms[selProg].name);
      break;
    case PAGE_AXIS:
      snprintf(dynTitle, sizeof(dynTitle), "Achse %c",
               (curAxis == AXIS_X) ? 'X' : (curAxis == AXIS_Y) ? 'Y' : 'Z');
      break;
    default: break;
  }
}

// ============================================================================
// WEITERE BILDSCHIRME
// ============================================================================

static void drawSplash() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.print("BA Hairpin");
  tft.setTextSize(1);
  tft.setTextColor(COL_SEL_BG);
  tft.setCursor(10, 54);
  tft.print("Vereinzelung  v0.3");

  int16_t y = 74;
  bool anyLocked = false;
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (!buttons_isLocked((ButtonId)i)) continue;
    if (!anyLocked) {
      tft.setTextColor(COL_ALARM);
      tft.setCursor(10, y); tft.print("Taste klemmt:");
      y += 12; anyLocked = true;
    }
    tft.setTextColor(COL_ALARM);
    tft.setCursor(10, y);
    tft.print(buttons_name((ButtonId)i)); tft.print(" LOW");
    y += 12;
  }
  if (!anyLocked) {
    tft.setTextColor(COL_DIM);
    tft.setCursor(10, y);
    tft.print("Taste druecken...");
  }
}

static int  lastRunsShown  = -1;
static int  lastBlockShown = -1;

static void drawRunningStatic() {
  tft.fillScreen(COL_BG);
  drawHeader(gPrograms[program_runningIndex()].name);
  drawFooter("ENTER = NOT-HALT");
  lastRunsShown  = -1;
  lastBlockShown = -1;
}

static void drawRunningDynamic() {
  Program& pr = gPrograms[program_runningIndex()];
  int runs = program_remainingRuns();
  int blk  = program_currentBlock();
  if (runs == lastRunsShown && blk == lastBlockShown) return;
  lastRunsShown  = runs;
  lastBlockShown = blk;

  tft.fillRect(0, HEADER_H, scrW, scrH - HEADER_H - FOOTER_H, COL_BG);

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(PAD_X, HEADER_H + 4);
  tft.print("Block ");
  tft.print(blk + 1); tft.print('/'); tft.print(pr.blockCount);

  char desc[26];
  if (blk < pr.blockCount) block_describe(pr.blocks[blk], desc, sizeof(desc));
  else                     snprintf(desc, sizeof(desc), "-");
  tft.setTextColor(COL_TEXT);
  tft.setCursor(PAD_X, HEADER_H + 18);
  tft.print(desc);

  tft.setTextColor(COL_DIM);
  tft.setCursor(PAD_X, HEADER_H + 40);
  tft.print("Verbleibende Laeufe:");
  tft.setTextColor(COL_OK);
  tft.setTextSize(3);
  tft.setCursor(PAD_X, HEADER_H + 54);
  tft.print(runs);
}

static void drawHalted() {
  tft.fillScreen(COL_ALARM);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(14, 28);
  tft.print("NOT-HALT");
  tft.setTextSize(1);
  tft.setCursor(14, 56); tft.print("Programm abgebrochen,");
  tft.setCursor(14, 68); tft.print("alle Achsen gestoppt.");
  tft.setTextColor(0xFFE0);
  tft.setCursor(14, 92); tft.print("Taste druecken = weiter");
}

static void drawRename() {
  tft.fillScreen(COL_BG);
  drawHeader("Umbenennen");

  // Bei Textgroesse 2 passen zwoelf Zeichen nebeneinander. Laengere Namen
  // laufen im Fenster mit, das dem Cursor folgt.
  const uint8_t VIS = 12;
  uint8_t first = (namePos < VIS) ? 0 : (uint8_t)(namePos - VIS + 1);

  tft.setTextSize(2);
  int16_t x0 = PAD_X, y0 = HEADER_H + 20;
  for (uint8_t k = 0; k < VIS; k++) {
    uint8_t i = first + k;
    if (i >= PROG_NAME_LEN - 1) break;
    char c = nameBuf[i] ? nameBuf[i] : ' ';
    int16_t x = x0 + k * 12;
    tft.setTextColor(i == namePos ? COL_SEL_BG : COL_TEXT);
    tft.setCursor(x, y0);
    tft.print(c);
    if (i == namePos) tft.drawFastHLine(x, y0 + 18, 11, COL_SEL_BG);
  }

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(PAD_X, HEADER_H + 52);  tft.print("UP/DOWN  Zeichen");
  tft.setCursor(PAD_X, HEADER_H + 64);  tft.print("L/R      Position");
  tft.setTextColor(COL_OK);
  tft.setCursor(PAD_X, HEADER_H + 78);  tft.print("ENTER    uebernehmen");
  drawFooter("");
}

static void drawHelp() {
  char t[26];
  snprintf(t, sizeof(t), "%s  %u/%u", HELP[helpPage].title,
           (unsigned)(helpPage + 1), (unsigned)HELP_COUNT);
  tft.fillScreen(COL_BG);
  drawHeader(t);

  tft.setTextSize(1);
  int16_t y = HEADER_H + 3;
  for (uint8_t i = 0; i < 8; i++) {
    const char* ln = HELP[helpPage].lines[i];
    if (!ln) break;
    tft.setTextColor(COL_TEXT);
    tft.setCursor(PAD_X, y);
    tft.print(ln);
    y += 12;
  }
  drawFooter("L/R blaettern  ENTER ok");
}

static void drawConfirm() {
  tft.fillScreen(COL_BG);
  drawHeader("Bestaetigen");
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(PAD_X, HEADER_H + 16);
  tft.print(confirmText);
  if (confirmAct == ACT_NONE) {
    tft.setTextColor(COL_DIM);
    tft.setCursor(PAD_X, HEADER_H + 44);
    tft.print("ENTER = weiter");
  } else {
    tft.setTextColor(COL_ALARM);
    tft.setCursor(PAD_X, HEADER_H + 44);
    tft.print("ENTER = ja");
    tft.setTextColor(COL_DIM);
    tft.setCursor(PAD_X, HEADER_H + 58);
    tft.print("LEFT  = abbrechen");
  }
  drawFooter("");
}

// ============================================================================
// NAVIGATION UND AKTIONEN
// ============================================================================

static bool rowSelectable(const MenuRow& r);
static void moveCursor(int dir);

static void openPage(uint8_t page, bool push = true) {
  if (push && navDepth < (sizeof(navStack) / sizeof(navStack[0])))
    navStack[navDepth++] = { curPage, curRow, scrollTop };
  curPage       = page;
  curRow        = 0;
  scrollTop     = 0;
  currentScreen = SCR_PAGE;
  buildDynamicPage(page);
  // Erste Zeile kann eine Ueberschrift sein
  if (pageCount() && !rowSelectable(pageRows()[0])) moveCursor(+1);
  needsRedraw   = true;
}

static void goBack() {
  if (navDepth == 0) return;
  NavEntry e = navStack[--navDepth];
  curPage       = e.page;
  curRow        = e.row;
  scrollTop     = e.top;
  currentScreen = SCR_PAGE;
  buildDynamicPage(curPage);   // Inhalt kann sich geaendert haben
  needsRedraw   = true;
}

static void doHalt() {
  program_abort();
  currentScreen = SCR_HALTED;
  needsRedraw   = true;
}

static void askConfirm(const char* text, uint8_t action) {
  confirmText   = text;
  confirmAct    = action;
  currentScreen = SCR_CONFIRM;
  needsRedraw   = true;
}

// Reiner Hinweis ohne Rueckfrage - fuer Aktionen, die gerade nicht gehen.
static void showNote(const char* text) { askConfirm(text, ACT_NONE); }

static void runAction(uint8_t act, uint8_t arg, bool confirmed = false) {
  switch (act) {

    case ACT_OPEN_PROG:
      selProg = arg;
      openPage(PAGE_PROGRAM);
      break;

    case ACT_NEW_PROG: {
      // Vorlage ist Programm 3, der vollstaendigste Ablauf.
      int n = program_copy(program_templateIndex());
      if (n >= 0) { selProg = (uint8_t)n; openPage(PAGE_PROGRAM); }
      else        showNote("Kein Platz mehr frei.");
      break;
    }

    case ACT_START:
      program_start(selProg, (int)gPrograms[selProg].defaultRuns);
      currentScreen = SCR_RUNNING;
      needsRedraw   = true;
      break;

    case ACT_RENAME: {
      // Puffer vollstaendig mit Leerzeichen fuellen: sonst entstuende beim
      // Bearbeiten hinter dem Namensende eine Luecke, an der der Name spaeter
      // abgeschnitten wuerde.
      const char* cur = gPrograms[selProg].name;
      for (uint8_t i = 0; i < PROG_NAME_LEN - 1; i++)
        nameBuf[i] = (i < strlen(cur)) ? cur[i] : ' ';
      nameBuf[PROG_NAME_LEN - 1] = '\0';
      namePos       = 0;
      currentScreen = SCR_RENAME;
      needsRedraw   = true;
      break;
    }

    case ACT_SAVE:
      program_save();
      needsRedraw = true;
      break;

    case ACT_DELETE:
      if (!confirmed) {
        if (program_count() <= 1) { showNote("Letztes Programm bleibt."); break; }
        askConfirm("Programm loeschen?", ACT_DELETE);
        break;
      }
      program_remove(selProg);
      // Navigationsstack neu aufsetzen: unter der Programmliste muss das
      // Hauptmenue liegen, sonst kommt man von dort nicht mehr zurueck.
      navDepth    = 1;
      navStack[0] = { PAGE_MAIN, 0, 0 };
      openPage(PAGE_PROGRAMS, false);
      break;

    case ACT_OPEN_BLOCK:
      selBlock = arg;
      openPage(PAGE_BLOCK);
      break;

    case ACT_AXIS_X: case ACT_AXIS_Y: case ACT_AXIS_Z:
      curAxis = (act == ACT_AXIS_X) ? AXIS_X
              : (act == ACT_AXIS_Y) ? AXIS_Y : AXIS_Z;
      openPage(PAGE_AXIS);
      break;

    case ACT_HOME_ALL:
      axis_home(AXIS_X); axis_home(AXIS_Y); axis_home(AXIS_Z);
      break;

    case ACT_DRV_ON:    axis_enable();  break;
    case ACT_DRV_OFF:   axis_disable(); break;
    case ACT_AXIS_HOME: axis_home(curAxis); break;
    case ACT_AXIS_STOP: axis_stop(curAxis); break;

    case ACT_SERVO_APPLY:
      servo_set(arg, (uint16_t)servoVal[arg]);
      break;

    case ACT_SERVO_INIT: {
      static const int32_t INIT_VALS[6] = { 1000, 100, 0, 800, 500, 500 };
      for (uint8_t i = 0; i < 6; i++) {
        servoVal[i] = INIT_VALS[i];
        servo_set(i, (uint16_t)INIT_VALS[i]);
      }
      needsRedraw = true;
      break;
    }

    case ACT_HELP:
      helpPage      = 0;
      currentScreen = SCR_HELP;
      needsRedraw   = true;
      break;

    case ACT_FACTORY:
      if (!confirmed) { askConfirm("Alles zuruecksetzen?", ACT_FACTORY); break; }
      program_resetAll();
      selProg  = program_templateIndex();
      selBlock = 0;
      navDepth = 0;
      openPage(PAGE_MAIN, false);
      break;

    case ACT_HALT: doHalt(); break;
    default: break;
  }
}

static void changeRow(const MenuRow& r, int dir) {
  if (r.type == ROW_JOG) {
    axis_rel(curAxis, dir * JOG_STEPS[jogStepIdx], (int16_t)jogSpeed);
    return;
  }
  if (!rowIsEditable(r)) return;

  // Beschleunigen, solange die Taste gehalten wird
  unsigned long now = millis();
  if ((now - editLast) > 400 || dir != editDir) editRepeat = 0;
  else if (editRepeat < 100)                    editRepeat++;
  editLast = now;
  editDir  = dir;
  int32_t mult = (editRepeat < 6) ? 1 : (editRepeat < 20) ? 10 : 100;

  int32_t cur = rowGet(r);
  int32_t step = (r.type == ROW_CHOICE) ? 1 : r.vstep * mult;
  int32_t v = cur + dir * step;
  if (v < r.vmin) v = r.vmin;
  if (v > r.vmax) v = r.vmax;
  if (v == cur) return;

  rowSet(r, v);
  if (r.action != ACT_NONE) runAction(r.action, r.arg);

  // Beim Wechsel der Servo-Nummer gelten andere Stellwert-Grenzen,
  // deshalb die Seite neu aufbauen.
  if (r.type == ROW_BLOCKFIELD && r.arg == BF_IDX && curPage == PAGE_BLOCK) {
    uint8_t keep = curRow;
    buildDynamicPage(PAGE_BLOCK);
    if (keep < pageCount()) curRow = keep;
    needsRedraw = true;
    return;
  }
  needsRows = true;
}

// Ueberschriften und Infozeilen sind nicht anwaehlbar
static bool rowSelectable(const MenuRow& r) {
  return r.type != ROW_GROUP && r.type != ROW_INFO;
}

// Naechste anwaehlbare Zeile in Richtung dir suchen
static void moveCursor(int dir) {
  uint8_t cnt = pageCount();
  if (cnt == 0) return;
  const MenuRow* rows = pageRows();
  for (uint8_t n = 0; n < cnt; n++) {
    curRow = (dir > 0) ? (uint8_t)((curRow + 1) % cnt)
                       : (uint8_t)((curRow == 0) ? cnt - 1 : curRow - 1);
    if (rowSelectable(rows[curRow])) break;
  }
  needsRows = true;
}

static void handlePageInput(ButtonId ev) {
  uint8_t cnt = pageCount();
  if (cnt == 0) return;
  if (curRow >= cnt) curRow = cnt - 1;   // Seite kann kuerzer geworden sein
  const MenuRow& r = pageRows()[curRow];

  switch (ev) {
    case BTN_UP:
      moveCursor(-1);
      break;
    case BTN_DOWN:
      moveCursor(+1);
      break;
    case BTN_LEFT:
      if (rowConsumesLeftRight(r)) changeRow(r, -1);
      else                        goBack();
      break;
    case BTN_RIGHT:
      if (rowConsumesLeftRight(r))    changeRow(r, +1);
      else if (r.type == ROW_SUBMENU) openPage(r.action);
      break;
    case BTN_ENTER:
      if (r.type == ROW_SUBMENU)     openPage(r.action);
      else if (r.type == ROW_BACK)   goBack();
      else if (r.type == ROW_ACTION) runAction(r.action, r.arg);
      break;
    default: break;
  }
}

// ============================================================================
// PANEL-INITIALISIERUNG
// ============================================================================
// Die Bibliothek faehrt ihre Init-Sequenz fest mit 32 MHz (SPI_DEFAULT_FREQ in
// Adafruit_ST77xx.cpp); das laesst sich von aussen nicht setzen. Bei langen
// Kabeln kommen die Befehle verstuemmelt an und das Panel bleibt schwarz.
// Deshalb wird danach auf TFT_SPI_HZ heruntergeschaltet und die
// Einschaltbefehle werden noch einmal gesendet. Die delay() sind
// Datenblatt-Wartezeiten und laufen einmalig in setup().
static void tftInitPanel(uint8_t tabType) {
  tft.initR(tabType);
  tft.setSPISpeed(TFT_SPI_HZ);

  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(150);
  uint8_t colmod = 0x05;
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(100);

  tft.setRotation(TFT_ROTATION);
#if TFT_INVERT_COLORS
  tft.invertDisplay(true);
#endif
  scrW = tft.width();
  scrH = tft.height();
}

// ============================================================================
// OEFFENTLICHE API
// ============================================================================

void ui_begin() {
  buttons_begin();

#if TFT_BL_CONTROLLED
  pinMode(TFT_PIN_BL, OUTPUT);
  digitalWrite(TFT_PIN_BL, HIGH);
#endif
#if !TFT_USE_SOFT_SPI
  SPI.begin(TFT_PIN_SCLK, -1, TFT_PIN_MOSI, TFT_PIN_CS);
#endif

  tftInitPanel(TFT_TAB_TYPE);

  currentScreen = SCR_SPLASH;
  splashStart   = millis();
  lastActivity  = millis();
  needsRedraw   = true;

  Serial.print(F("[UI] Display bereit: "));
  Serial.print(scrW); Serial.print('x'); Serial.println(scrH);
}

void ui_update() {
  ButtonId ev = buttons_update();
  if (ev != BTN_NONE) {
    Serial.print(F("[BTN] ")); Serial.println(buttons_name(ev));
    lastActivity = millis();
    if (driversOff) {
      axis_enable();
      driversOff  = false;
      needsHeader = true;
      Serial.println(F("[UI] Motortreiber wieder eingeschaltet."));
    }
  }
  // Waehrend ein Programm laeuft, gilt das System nicht als unbenutzt
  if (program_isRunning()) lastActivity = millis();

  // --- NOT-HALT als globale Geste: LEFT 1,5 s halten ---
  // Auf Zeilen, in denen LEFT einen Wert verkleinert, gesperrt: dort haelt man
  // die Taste absichtlich. Bewaffnet wird der Timer nur durch ein echtes
  // Druck-Ereignis, nicht durch den blossen Pegel.
  bool leftEdits = (currentScreen == SCR_PAGE) && pageCount()
                   && rowConsumesLeftRight(pageRows()[curRow]);
  if (!buttons_isDown(BTN_LEFT) || leftEdits
      || currentScreen == SCR_HALTED || currentScreen == SCR_CONFIRM
      || currentScreen == SCR_RENAME || currentScreen == SCR_HELP) {
    leftHoldStart = 0;
  } else {
    if (ev == BTN_LEFT && leftHoldStart == 0) leftHoldStart = millis();
    if (leftHoldStart && (millis() - leftHoldStart) >= HALT_HOLD_MS) {
      leftHoldStart = 0;
      doHalt();
      return;
    }
  }

  pollStatus();

  // --- Leerlauf ---
  unsigned long idleMs = millis() - lastActivity;

  if (!driversOff && idleMs >= IDLE_MOTORS_MS) {
    // Auch ohne erreichbaren Uno merken, sonst wuerde bei jedem Durchlauf
    // erneut ein Befehl abgesetzt, der ohnehin fehlschlaegt.
    driversOff  = true;
    needsHeader = true;
    if (unoOnline) axis_disable();
    Serial.println(F("[UI] 10 min ohne Eingabe - Motortreiber stromlos."));
  }

  // Zurueck zum Hauptbildschirm. Der Namens-Editor ist ausgenommen, damit
  // eine angefangene Eingabe nicht verloren geht.
  if (idleMs >= IDLE_HOME_MS && currentScreen != SCR_SPLASH
      && currentScreen != SCR_RUNNING && currentScreen != SCR_RENAME) {
    if (currentScreen != SCR_PAGE || curPage != PAGE_MAIN || navDepth != 0) {
      navDepth      = 0;
      curPage       = PAGE_MAIN;
      curRow        = 0;
      scrollTop     = 0;
      currentScreen = SCR_PAGE;
      buildDynamicPage(PAGE_MAIN);
      needsRedraw   = true;
    }
  }

  // --- Startbild ---
  if (currentScreen == SCR_SPLASH) {
    if (needsRedraw) { drawSplash(); needsRedraw = false; }
    unsigned long showMs = SPLASH_MS;
    for (uint8_t i = 0; i < BTN_COUNT; i++)
      if (buttons_isLocked((ButtonId)i)) { showMs = 8000; break; }
    if (ev != BTN_NONE || (millis() - splashStart) >= showMs) {
      curPage = PAGE_MAIN; curRow = 0; scrollTop = 0; navDepth = 0;
      currentScreen = SCR_PAGE;
      needsRedraw   = true;
    }
    return;
  }

  // --- Hilfe ---
  if (currentScreen == SCR_HELP) {
    if (needsRedraw) { drawHelp(); needsRedraw = false; }
    if (ev == BTN_RIGHT || ev == BTN_DOWN) {
      helpPage = (helpPage + 1) % HELP_COUNT;
      needsRedraw = true;
    } else if (ev == BTN_LEFT || ev == BTN_UP) {
      helpPage = (helpPage == 0) ? HELP_COUNT - 1 : helpPage - 1;
      needsRedraw = true;
    } else if (ev == BTN_ENTER) {
      currentScreen = SCR_PAGE;
      needsRedraw   = true;
    }
    return;
  }

  // --- Namens-Editor ---
  if (currentScreen == SCR_RENAME) {
    if (needsRedraw) { drawRename(); needsRedraw = false; }
    if (ev == BTN_LEFT || ev == BTN_RIGHT) {
      uint8_t last = PROG_NAME_LEN - 2;
      if (ev == BTN_RIGHT) namePos = (namePos >= last) ? 0 : namePos + 1;
      else                 namePos = (namePos == 0) ? last : namePos - 1;
      needsRedraw = true;
    } else if (ev == BTN_UP || ev == BTN_DOWN) {
      // aktuelles Zeichen im Zeichensatz weiterdrehen
      char c = nameBuf[namePos] ? nameBuf[namePos] : ' ';
      const char* pos = strchr(NAME_CHARS, c);
      int k = pos ? (int)(pos - NAME_CHARS) : 0;
      k += (ev == BTN_UP) ? 1 : -1;
      if (k < 0) k = NAME_CHAR_COUNT - 1;
      if (k >= (int)NAME_CHAR_COUNT) k = 0;
      nameBuf[namePos] = NAME_CHARS[k];
      needsRedraw = true;
    } else if (ev == BTN_ENTER) {
      // Leerzeichen am Ende entfernen, leeren Namen nicht zulassen
      nameBuf[PROG_NAME_LEN - 1] = '\0';
      for (int i = (int)strlen(nameBuf) - 1; i >= 0 && nameBuf[i] == ' '; i--)
        nameBuf[i] = '\0';
      if (nameBuf[0] == '\0')
        snprintf(nameBuf, PROG_NAME_LEN, "Programm %u", (unsigned)(selProg + 1));
      strncpy(gPrograms[selProg].name, nameBuf, PROG_NAME_LEN - 1);
      gPrograms[selProg].name[PROG_NAME_LEN - 1] = '\0';
      program_markDirty();
      currentScreen = SCR_PAGE;
      buildDynamicPage(curPage);
      needsRedraw   = true;
    }
    return;
  }

  // --- Bestaetigungsdialog ---
  if (currentScreen == SCR_CONFIRM) {
    if (needsRedraw) { drawConfirm(); needsRedraw = false; }
    if (ev == BTN_ENTER) {
      if (confirmAct == ACT_NONE) { currentScreen = SCR_PAGE; needsRedraw = true; }
      else                        runAction(confirmAct, 0, true);
    } else if (ev == BTN_LEFT) { currentScreen = SCR_PAGE; needsRedraw = true; }
    return;
  }

  // --- Bestaetigung nach NOT-HALT ---
  if (currentScreen == SCR_HALTED) {
    if (needsRedraw) { drawHalted(); needsRedraw = false; }
    if (ev != BTN_NONE) { currentScreen = SCR_PAGE; needsRedraw = true; }
    return;
  }

  // --- Laufendes Programm: Navigation gesperrt, ENTER haelt sofort an ---
  if (program_isRunning()) {
    if (currentScreen != SCR_RUNNING) { currentScreen = SCR_RUNNING; needsRedraw = true; }
    if (needsRedraw) { drawRunningStatic(); needsRedraw = false; }
    drawRunningDynamic();
    if (ev == BTN_ENTER) doHalt();
    return;
  }
  if (currentScreen == SCR_RUNNING) { currentScreen = SCR_PAGE; needsRedraw = true; }

  // --- Menueseiten ---
  if (ev != BTN_NONE) handlePageInput(ev);

  // Hat die Eingabe den Bildschirm gewechselt (Editor, Rueckfrage, Start),
  // darf hier nicht mehr die Menueseite gezeichnet werden - sonst wuerde
  // deren needsRedraw verbraucht und der neue Bildschirm nie erscheinen.
  if (currentScreen != SCR_PAGE) return;

  if (!needsRedraw && !needsRows) {
    char now[sizeof(lastStatusText)];
    buildStatusText(now, sizeof(now));
    if (strcmp(now, lastStatusText) != 0) needsHeader = true;
  }

  if (needsRedraw) {
    drawPage();
    needsRedraw = false; needsRows = false; needsHeader = false;
  } else if (needsRows) {
    drawRows();
    drawFooter(footerHint());
    drawScrollMark();
    needsRows = false;
  } else if (needsHeader) {
    char title[26];
    composeTitle(title, sizeof(title));
    drawHeader(title);
    needsHeader = false;
  }
}
