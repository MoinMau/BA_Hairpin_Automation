#include "display_ui.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#include "config_display.h"
#include "buttons.h"
#include "machine_api.h"

// ----------------------------------------------------------------------------
// Display-Objekt.
//   Software-SPI: bitbang ueber MOSI/SCLK, ca. 1-2 MHz, sehr tolerant
//                 gegenueber langen Kabeln.
//   Hardware-SPI: VSPI, deutlich schneller, aber empfindlicher.
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
#define COL_DIM        0x8410            // mittleres Grau
#define COL_HEADER_BG  0x001F            // Blau
#define COL_SEL_BG     0xFD20            // Orange
#define COL_SEL_TEXT   ST77XX_BLACK
#define COL_VALUE      0x07FF            // Cyan
#define COL_OK         ST77XX_GREEN
#define COL_ALARM      ST77XX_RED

// --- Layout (Querformat 160x128) ---
static int16_t scrW = 160;
static int16_t scrH = 128;
#define HEADER_H   14
#define FOOTER_H   12
#define ROW_H      14
#define PAD_X       5
#define CHAR_W      6    // Breite eines Zeichens bei Textgroesse 1

// ============================================================================
// MENUE-MODELL
// ----------------------------------------------------------------------------
// Das gesamte Menue steckt in Tabellen. Ein Renderer und ein Navigationsstack
// bedienen alle Seiten. Ein neuer Menuepunkt ist damit eine Tabellenzeile.
// ============================================================================

enum RowType {
  ROW_SUBMENU,   // ENTER/RIGHT oeffnet eine Unterseite
  ROW_ACTION,    // ENTER loest eine Aktion aus
  ROW_VALUE,     // LEFT/RIGHT aendert einen Zahlenwert
  ROW_CHOICE,    // LEFT/RIGHT waehlt aus einer festen Liste
  ROW_JOG,       // LEFT/RIGHT verfaehrt die Achse um die Schrittweite
  ROW_BACK       // ENTER/LEFT geht eine Ebene zurueck
};

// Seiten-IDs
enum PageId {
  PAGE_MAIN = 0,
  PAGE_PROGRAMS,
  PAGE_PROGRUN,
  PAGE_STEPPERS,
  PAGE_AXIS,
  PAGE_SERVOS,
  PAGE_COUNT
};

// Aktions-IDs
enum ActionId {
  ACT_NONE = 0,
  ACT_SEL_P1, ACT_SEL_P2, ACT_SEL_P3,
  ACT_START,
  ACT_AXIS_X, ACT_AXIS_Y, ACT_AXIS_Z,
  ACT_HOME_ALL, ACT_DRV_ON, ACT_DRV_OFF,
  ACT_AXIS_HOME, ACT_AXIS_STOP,
  ACT_SERVO_APPLY, ACT_SERVO_INIT, ACT_SERVO_ALL_OFF,
  ACT_HALT
};

struct MenuRow {
  const char*     label;
  RowType         type;
  uint8_t         action;      // ACT_* bzw. PAGE_* bei ROW_SUBMENU
  uint8_t         arg;         // z.B. Servo-Nummer
  int32_t*        value;       // ROW_VALUE / ROW_CHOICE
  int32_t         vmin, vmax, vstep;
  const int32_t*  choices;     // ROW_CHOICE
  uint8_t         choiceCount;
};

struct MenuPage {
  const char*    title;        // NULL -> dynamischer Titel
  const MenuRow* rows;
  uint8_t        count;
};

// --- Kuerzel, damit die Tabellen lesbar bleiben ---
#define R_SUB(lbl, page)                 { lbl, ROW_SUBMENU, page, 0, nullptr, 0,0,0, nullptr, 0 }
#define R_ACT(lbl, act)                  { lbl, ROW_ACTION,  act,  0, nullptr, 0,0,0, nullptr, 0 }
#define R_ACTA(lbl, act, a)              { lbl, ROW_ACTION,  act,  a, nullptr, 0,0,0, nullptr, 0 }
#define R_VAL(lbl, var, lo, hi, st)      { lbl, ROW_VALUE, ACT_NONE, 0, &(var), lo, hi, st, nullptr, 0 }
#define R_VALA(lbl, var, lo, hi, st, act, a) { lbl, ROW_VALUE, act, a, &(var), lo, hi, st, nullptr, 0 }
#define R_CHO(lbl, var, arr)             { lbl, ROW_CHOICE, ACT_NONE, 0, &(var), 0, (int32_t)(sizeof(arr)/sizeof(arr[0]))-1, 1, arr, sizeof(arr)/sizeof(arr[0]) }
#define R_JOG(lbl)                       { lbl, ROW_JOG, ACT_NONE, 0, nullptr, 0,0,0, nullptr, 0 }
#define R_BACK()                         { "Zurueck", ROW_BACK, ACT_NONE, 0, nullptr, 0,0,0, nullptr, 0 }

// ============================================================================
// EINSTELLBARE WERTE
// ============================================================================

static int32_t progRuns   = 1;      // Durchlaeufe des gewaehlten Programms
static int32_t jogStepIdx = 2;      // Index in JOG_STEPS -> 100 Steps
static int32_t jogSpeed   = 800;    // Steps/s fuer Handfahrt
static int32_t servoVal[6] = { 500, 500, 500, 500, 500, 500 };

static const int32_t JOG_STEPS[] = { 1, 10, 100, 1000 };

static uint8_t selProgram = 1;      // im Programm-Menue gewaehltes Programm
static uint8_t curAxis    = AXIS_X; // im Achs-Menue gewaehlte Achse

// ============================================================================
// SEITEN-TABELLEN
// ============================================================================

// --- Hauptmenue ---
static const MenuRow ROWS_MAIN[] = {
  R_SUB("Programme",      PAGE_PROGRAMS),
  R_SUB("Schrittmotoren", PAGE_STEPPERS),
  R_SUB("Servomotoren",   PAGE_SERVOS),
  R_ACT("NOT-HALT",       ACT_HALT)
};

// --- Programme ---
static const MenuRow ROWS_PROGRAMS[] = {
  R_ACT("Programm 1  Hairpin", ACT_SEL_P1),
  R_ACT("Programm 2",          ACT_SEL_P2),
  R_ACT("Programm 3",          ACT_SEL_P3),
  R_BACK()
};

// --- Start-Seite eines Programms (Titel wird zur Laufzeit gesetzt) ---
static const MenuRow ROWS_PROGRUN[] = {
  R_VAL("Durchlaeufe", progRuns, 1, 99, 1),
  R_ACT("START",       ACT_START),
  R_BACK()
};

// --- Schrittmotoren ---
static const MenuRow ROWS_STEPPERS[] = {
  R_ACT("Achse X",            ACT_AXIS_X),
  R_ACT("Achse Y",            ACT_AXIS_Y),
  R_ACT("Achse Z",            ACT_AXIS_Z),
  R_ACT("Referenzfahrt alle", ACT_HOME_ALL),
  R_ACT("Treiber EIN",        ACT_DRV_ON),
  R_ACT("Treiber AUS",        ACT_DRV_OFF),
  R_BACK()
};

// --- Einzelne Achse (Titel wird zur Laufzeit gesetzt) ---
static const MenuRow ROWS_AXIS[] = {
  R_ACT("Referenzfahrt",  ACT_AXIS_HOME),
  R_CHO("Schrittweite",   jogStepIdx, JOG_STEPS),
  R_VAL("Geschw. St/s",   jogSpeed, 50, 3000, 50),
  R_JOG("Fahren   - / +"),
  R_ACT("STOPP",          ACT_AXIS_STOP),
  R_BACK()
};

// --- Servomotoren ---
static const MenuRow ROWS_SERVOS[] = {
  R_VALA("Servo 0  D7",  servoVal[0], 0, 1000, 10, ACT_SERVO_APPLY, 0),
  R_VALA("Servo 1  D8",  servoVal[1], 0, 1000, 10, ACT_SERVO_APPLY, 1),
  R_VALA("Servo 2  D9",  servoVal[2], 0, 1000, 10, ACT_SERVO_APPLY, 2),
  R_VALA("Servo 3  D10", servoVal[3], 0, 1000, 10, ACT_SERVO_APPLY, 3),
  R_VALA("Servo 4  D11", servoVal[4], 0, 1000, 10, ACT_SERVO_APPLY, 4),
  R_VALA("Servo 5  D12", servoVal[5], 0, 1000, 10, ACT_SERVO_APPLY, 5),
  R_ACT("Grundstellung", ACT_SERVO_INIT),
  R_BACK()
};

static const MenuPage PAGES[PAGE_COUNT] = {
  { "Hauptmenue",     ROWS_MAIN,     sizeof(ROWS_MAIN)     / sizeof(MenuRow) },
  { "Programme",      ROWS_PROGRAMS, sizeof(ROWS_PROGRAMS) / sizeof(MenuRow) },
  { nullptr,          ROWS_PROGRUN,  sizeof(ROWS_PROGRUN)  / sizeof(MenuRow) },
  { "Schrittmotoren", ROWS_STEPPERS, sizeof(ROWS_STEPPERS) / sizeof(MenuRow) },
  { nullptr,          ROWS_AXIS,     sizeof(ROWS_AXIS)     / sizeof(MenuRow) },
  { "Servomotoren",   ROWS_SERVOS,   sizeof(ROWS_SERVOS)   / sizeof(MenuRow) }
};

// ============================================================================
// ZUSTAND DER OBERFLAECHE
// ============================================================================

enum UiScreen {
  SCR_SPLASH,    // Startbild
  SCR_PAGE,      // tabellengetriebene Menueseite
  SCR_RUNNING,   // laufende Sequenz
  SCR_HALTED     // Bestaetigung nach NOT-HALT
};

static UiScreen currentScreen = SCR_SPLASH;
static unsigned long splashStart = 0;
#define SPLASH_MS 2000

static uint8_t curPage   = PAGE_MAIN;
static uint8_t curRow    = 0;
static uint8_t scrollTop = 0;
static char    dynTitle[26] = "";

// Navigationsstack: merkt sich Seite und Cursorposition der Ebene darueber
struct NavEntry { uint8_t page; uint8_t row; uint8_t top; };
static NavEntry navStack[4];
static uint8_t  navDepth = 0;

static bool needsRedraw  = true;   // Vollbild
static bool needsRows    = false;  // nur die Zeilen
static bool needsHeader  = false;  // nur die Kopfzeile

// Zwischengespeicherter Maschinenstatus. Wird getaktet geholt, damit das
// Menue den I2C-Bus nicht zusaetzlich belastet.
static StepperStatus mStatus = { 0, 0, 0, 0, 0 };
static unsigned long mStatusLast = 0;
#define STATUS_POLL_MS 300

// Ist der Uno ueberhaupt am Bus? Ohne diese Pruefung liefe das Menue bei
// abgezogenen Slaves alle 300 ms in einen I2C-Timeout und wuerde ruckeln.
// So laesst sich die Bedienung auch ohne angeschlossene Slaves testen.
static bool          unoOnline     = false;
static unsigned long presenceLast  = 0;
#define PRESENCE_POLL_MS 2000

// NOT-HALT als globale Geste: LEFT gedrueckt halten wirkt auf jedem Bildschirm
static unsigned long leftHoldStart = 0;
#define HALT_HOLD_MS 1500

// ============================================================================
// ZEICHEN-HELFER
// ============================================================================

static uint8_t visibleRows() {
  return (scrH - HEADER_H - FOOTER_H) / ROW_H;
}

static void drawTextRight(const char* s, int16_t rightX, int16_t y, uint16_t col) {
  int16_t w = (int16_t)strlen(s) * CHAR_W;
  tft.setTextColor(col);
  tft.setCursor(rightX - w, y);
  tft.print(s);
}

static void pollStatus(bool force = false) {
  unsigned long now = millis();

  // Praesenz seltener pruefen als den Status
  if (presenceLast == 0 || (now - presenceLast) >= PRESENCE_POLL_MS) {
    presenceLast = now;
    bool was = unoOnline;
    unoOnline = i2c_devicePresent(I2C_ADDR_UNO);
    if (was != unoOnline) {
      Serial.print(F("[UI] Uno (0x33): "));
      Serial.println(unoOnline ? F("online") : F("nicht erreichbar"));
    }
  }
  if (!unoOnline) {
    mStatus = { 0, 0, 0, 0, 0 };
    return;
  }

  if (!force && (now - mStatusLast) < STATUS_POLL_MS) return;
  mStatusLast = now;
  mStatus = get_stepper_status();
}

// Rechter Teil der Kopfzeile. Auf der Achsseite ist die Istposition die
// wichtigste Information, sonst der Gesamtzustand der Maschine.
static void buildStatusText(char* out, size_t n) {
  if (!unoOnline) {
    snprintf(out, n, "KEIN I2C");
    return;
  }
  if (currentScreen == SCR_PAGE && curPage == PAGE_AXIS) {
    int32_t pos = (curAxis == AXIS_X) ? mStatus.current_pos_x
                : (curAxis == AXIS_Y) ? mStatus.current_pos_y
                                      : mStatus.current_pos_z;
    bool busy = (mStatus.axis_busy & (1 << curAxis)) != 0;
    snprintf(out, n, "%ld%s", (long)pos, busy ? " >" : "");
    return;
  }
  if (sequence_isRunning()) {
    snprintf(out, n, "P%d RUN", sequence_program());
  } else if (mStatus.axis_busy & 0x07) {
    snprintf(out, n, "%c%c%c",
             (mStatus.axis_busy & BUSY_X) ? 'X' : '.',
             (mStatus.axis_busy & BUSY_Y) ? 'Y' : '.',
             (mStatus.axis_busy & BUSY_Z) ? 'Z' : '.');
  } else {
    snprintf(out, n, "BEREIT");
  }
}

// Zuletzt gezeichneter Status - damit die Kopfzeile nur bei echter
// Aenderung neu gezeichnet wird und nicht flackert.
static char lastStatusText[14] = "";

// Kopfzeile: links der Seitentitel, rechts der Live-Zustand
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

// Position in der Liste, rechts in der Fusszeile. Steht bewusst dort und
// nicht neben den Zeilen, damit nichts die Werte ueberdeckt.
static void drawScrollMark() {
  const MenuPage& p = PAGES[curPage];
  if (p.count <= visibleRows()) return;
  char m[10];
  snprintf(m, sizeof(m), "%u/%u", (unsigned)(curRow + 1), (unsigned)p.count);
  drawTextRight(m, scrW - PAD_X, scrH - FOOTER_H + 3, COL_DIM);
}

// Rechts stehender Wert einer Zeile als Text aufbereiten
static void rowValueText(const MenuRow& r, char* out, size_t n) {
  switch (r.type) {
    case ROW_VALUE:
      snprintf(out, n, "%ld", (long)*r.value);
      break;
    case ROW_CHOICE:
      snprintf(out, n, "%ld", (long)r.choices[*r.value]);
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
  const MenuPage& p = PAGES[curPage];
  uint8_t vis = visibleRows();

  // Sichtfenster nachfuehren
  if (curRow < scrollTop)               scrollTop = curRow;
  if (curRow >= scrollTop + vis)        scrollTop = curRow - vis + 1;
  if (p.count <= vis)                   scrollTop = 0;

  tft.fillRect(0, HEADER_H, scrW, scrH - HEADER_H - FOOTER_H, COL_BG);
  tft.setTextSize(1);

  for (uint8_t i = 0; i < vis && (scrollTop + i) < p.count; i++) {
    uint8_t        idx = scrollTop + i;
    const MenuRow& r   = p.rows[idx];
    int16_t        y   = HEADER_H + i * ROW_H;
    bool           sel = (idx == curRow);

    if (sel) tft.fillRect(0, y, scrW, ROW_H - 1, COL_SEL_BG);

    // NOT-HALT faellt auch unmarkiert auf
    uint16_t labelCol = sel ? COL_SEL_TEXT
                            : (r.action == ACT_HALT && r.type == ROW_ACTION
                               ? COL_ALARM : COL_TEXT);
    tft.setTextColor(labelCol);
    tft.setCursor(PAD_X, y + 3);
    tft.print(r.label);

    char val[12];
    rowValueText(r, val, sizeof(val));
    if (val[0]) drawTextRight(val, scrW - PAD_X, y + 3,
                              sel ? COL_SEL_TEXT : COL_VALUE);
  }

}

// Fusszeile passend zum markierten Zeilentyp
static const char* footerHint() {
  const MenuRow& r = PAGES[curPage].rows[curRow];
  switch (r.type) {
    case ROW_VALUE:
    case ROW_CHOICE: return "L/R aendern  ENTER ok";
    case ROW_JOG:    return "L/R = Achse verfahren";
    case ROW_SUBMENU:return "ENTER oeffnen";
    case ROW_BACK:   return "ENTER zurueck";
    default:         return "ENTER ausloesen";
  }
}

static void drawPage() {
  const MenuPage& p = PAGES[curPage];
  drawHeader(p.title ? p.title : dynTitle);
  drawRows();
  drawFooter(footerHint());
  drawScrollMark();
}

// ============================================================================
// WEITERE BILDSCHIRME
// ============================================================================

static void drawSplash() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 34);
  tft.print("BA Hairpin");
  tft.setTextSize(1);
  tft.setTextColor(COL_SEL_BG);
  tft.setCursor(10, 58);
  tft.print("Vereinzelung  v0.2");
  // Verdrahtungsfehler sofort sichtbar machen, ohne Serial-Monitor
  int16_t y = 78;
  bool anyLocked = false;
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (!buttons_isLocked((ButtonId)i)) continue;
    if (!anyLocked) {
      tft.setTextColor(COL_ALARM);
      tft.setCursor(10, y);
      tft.print("Taste klemmt:");
      y += 12;
      anyLocked = true;
    }
    tft.setTextColor(COL_ALARM);
    tft.setCursor(10, y);
    tft.print(buttons_name((ButtonId)i));
    tft.print(" dauerhaft LOW");
    y += 12;
  }
  if (!anyLocked) {
    tft.setTextColor(COL_DIM);
    tft.setCursor(10, y);
    tft.print("Taste druecken...");
  }
}

static int lastRunsShown = -1;

static void drawRunningStatic() {
  tft.fillScreen(COL_BG);
  drawHeader("LAEUFT");
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(PAD_X, HEADER_H + 10);
  tft.print("Programm ");
  tft.print(sequence_program());
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(PAD_X, HEADER_H + 36);
  tft.print("Verbleibende Laeufe:");
  drawFooter("ENTER = NOT-HALT");
  lastRunsShown = -1;
}

static void drawRunningDynamic() {
  int runs = sequence_remainingRuns();
  if (runs == lastRunsShown) return;
  lastRunsShown = runs;

  tft.fillRect(PAD_X, HEADER_H + 48, 60, 22, COL_BG);
  tft.setTextColor(COL_OK);
  tft.setTextSize(3);
  tft.setCursor(PAD_X, HEADER_H + 48);
  tft.print(runs);
}

static void drawHalted() {
  tft.fillScreen(COL_ALARM);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(14, 28);
  tft.print("NOT-HALT");
  tft.setTextSize(1);
  tft.setCursor(14, 56);
  tft.print("Sequenz abgebrochen,");
  tft.setCursor(14, 68);
  tft.print("alle Achsen gestoppt.");
  tft.setTextColor(0xFFE0);
  tft.setCursor(14, 92);
  tft.print("Taste druecken = weiter");
}

// ============================================================================
// NAVIGATION UND AKTIONEN
// ============================================================================

static void setDynTitle(const char* t) {
  strncpy(dynTitle, t, sizeof(dynTitle) - 1);
  dynTitle[sizeof(dynTitle) - 1] = '\0';
}

// Ist-Stellwerte vom Nano holen, damit die Servo-Seite nicht Werte anzeigt,
// die nie gesendet wurden. Antwortet der Nano nicht, bleiben die bisherigen
// Werte stehen - besser als eine Null anzuzeigen, die nicht stimmt.
static void refreshServoValues() {
  uint16_t v[6];
  if (!servo_readAll(v)) {
    Serial.println(F("[UI] Servo-Istwerte: Nano antwortet nicht."));
    return;
  }
  for (uint8_t i = 0; i < 6; i++) servoVal[i] = (int32_t)v[i];
}

static void openPage(uint8_t page, bool push = true) {
  if (push && navDepth < (sizeof(navStack) / sizeof(navStack[0]))) {
    navStack[navDepth++] = { curPage, curRow, scrollTop };
  }
  if (page == PAGE_SERVOS) refreshServoValues();

  curPage     = page;
  curRow      = 0;
  scrollTop   = 0;
  currentScreen = SCR_PAGE;
  needsRedraw = true;
}

static void goBack() {
  if (navDepth == 0) return;
  NavEntry e = navStack[--navDepth];
  curPage     = e.page;
  curRow      = e.row;
  scrollTop   = e.top;
  currentScreen = SCR_PAGE;
  needsRedraw = true;
}

static void doHalt() {
  sequence_abort();
  currentScreen = SCR_HALTED;
  needsRedraw   = true;
}

static void runAction(uint8_t act, uint8_t arg) {
  char buf[26];
  switch (act) {

    // --- Programmauswahl ---
    case ACT_SEL_P1: case ACT_SEL_P2: case ACT_SEL_P3:
      selProgram = (act == ACT_SEL_P1) ? 1 : (act == ACT_SEL_P2) ? 2 : 3;
      snprintf(buf, sizeof(buf), "Programm %d", selProgram);
      setDynTitle(buf);
      openPage(PAGE_PROGRUN);
      break;

    case ACT_START:
      startHairpinSequence(selProgram, (int)progRuns);
      currentScreen = SCR_RUNNING;
      needsRedraw   = true;
      break;

    // --- Achsauswahl ---
    case ACT_AXIS_X: case ACT_AXIS_Y: case ACT_AXIS_Z:
      curAxis = (act == ACT_AXIS_X) ? AXIS_X : (act == ACT_AXIS_Y) ? AXIS_Y : AXIS_Z;
      snprintf(buf, sizeof(buf), "Achse %c",
               (curAxis == AXIS_X) ? 'X' : (curAxis == AXIS_Y) ? 'Y' : 'Z');
      setDynTitle(buf);
      openPage(PAGE_AXIS);
      break;

    case ACT_HOME_ALL:
      axis_home(AXIS_X); axis_home(AXIS_Y); axis_home(AXIS_Z);
      break;

    case ACT_DRV_ON:  axis_enable();  break;
    case ACT_DRV_OFF: axis_disable(); break;

    case ACT_AXIS_HOME: axis_home(curAxis); break;
    case ACT_AXIS_STOP: axis_stop(curAxis); break;

    // --- Servos ---
    case ACT_SERVO_APPLY:
      servo_set(arg, (uint16_t)servoVal[arg]);
      break;

    case ACT_SERVO_INIT: {
      // Grundstellung wie am Anfang von Programm 1
      static const int32_t INIT_VALS[6] = { 1000, 100, 0, 800, 500, 500 };
      for (uint8_t i = 0; i < 6; i++) {
        servoVal[i] = INIT_VALS[i];
        servo_set(i, (uint16_t)INIT_VALS[i]);
      }
      needsRedraw = true;
      break;
    }

    case ACT_HALT: doHalt(); break;

    default: break;
  }
}

// Wert einer Zeile veraendern. dir ist -1 oder +1.
static void changeRow(const MenuRow& r, int dir) {
  switch (r.type) {
    case ROW_VALUE: {
      int32_t v = *r.value + dir * r.vstep;
      if (v < r.vmin) v = r.vmin;
      if (v > r.vmax) v = r.vmax;
      if (v == *r.value) return;
      *r.value = v;
      if (r.action != ACT_NONE) runAction(r.action, r.arg);
      needsRows = true;
      break;
    }
    case ROW_CHOICE: {
      int32_t v = *r.value + dir;
      if (v < 0) v = 0;
      if (v > r.vmax) v = r.vmax;
      if (v == *r.value) return;
      *r.value = v;
      needsRows = true;
      break;
    }
    case ROW_JOG:
      axis_rel(curAxis, dir * JOG_STEPS[jogStepIdx], (int16_t)jogSpeed);
      break;
    default:
      break;
  }
}

static bool rowConsumesLeftRight(const MenuRow& r) {
  return r.type == ROW_VALUE || r.type == ROW_CHOICE || r.type == ROW_JOG;
}

static void handlePageInput(ButtonId ev) {
  const MenuPage& p = PAGES[curPage];
  const MenuRow&  r = p.rows[curRow];

  switch (ev) {
    case BTN_UP:
      curRow    = (curRow == 0) ? (p.count - 1) : (curRow - 1);
      needsRows = true;
      break;

    case BTN_DOWN:
      curRow    = (curRow + 1) % p.count;
      needsRows = true;
      break;

    case BTN_LEFT:
      if (rowConsumesLeftRight(r)) changeRow(r, -1);
      else                        goBack();
      break;

    case BTN_RIGHT:
      if (rowConsumesLeftRight(r))       changeRow(r, +1);
      else if (r.type == ROW_SUBMENU)    openPage(r.action);
      break;

    case BTN_ENTER:
      if (r.type == ROW_SUBMENU)     openPage(r.action);
      else if (r.type == ROW_BACK)   goBack();
      else if (r.type == ROW_ACTION) runAction(r.action, r.arg);
      break;

    default:
      break;
  }
}

// ============================================================================
// PANEL-INITIALISIERUNG
// ============================================================================
// Die Bibliothek faehrt ihre Init-Sequenz fest mit 32 MHz (SPI_DEFAULT_FREQ in
// Adafruit_ST77xx.cpp) - das laesst sich von aussen nicht setzen. Bei langen
// Kabeln kommen diese Befehle verstuemmelt an und das Panel bleibt schwarz.
// Deshalb wird direkt danach auf TFT_SPI_HZ heruntergeschaltet und die
// entscheidenden Einschaltbefehle werden noch einmal gesendet.
//
// Die delay() hier sind Datenblatt-Wartezeiten des ST7735 und laufen
// ausschliesslich einmalig in setup(), bevor die Maschine arbeitet.
static void tftInitPanel(uint8_t tabType) {
  tft.initR(tabType);
  tft.setSPISpeed(TFT_SPI_HZ);

  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(150);
  uint8_t colmod = 0x05;                        // 16 Bit pro Pixel
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1);   delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(100);

  tft.setRotation(TFT_ROTATION);   // sendet MADCTL erneut
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
  needsRedraw   = true;

  Serial.print(F("[UI] Display bereit: "));
  Serial.print(scrW); Serial.print('x'); Serial.println(scrH);
}

void ui_update() {
  ButtonId ev = buttons_update();
  if (ev != BTN_NONE) {
    Serial.print(F("[BTN] ")); Serial.println(buttons_name(ev));
  }

  // --- NOT-HALT als globale Geste: LEFT 1,5 s halten ---
  // Wirkt auf jedem Bildschirm, auch tief in einem Untermenue. Bewusst als
  // Halte-Geste, damit ein versehentlicher kurzer Druck nichts abbricht.
  //
  // Ausgenommen sind Zeilen, in denen LEFT einen Wert verkleinert: dort haelt
  // man die Taste absichtlich laenger gedrueckt (Autorepeat), und das darf
  // keinen Abbruch ausloesen.
  bool leftEditsValue = (currentScreen == SCR_PAGE)
                        && rowConsumesLeftRight(PAGES[curPage].rows[curRow]);

  // Der Timer wird nur durch ein echtes Druck-Ereignis bewaffnet, nicht durch
  // den blossen Pegel. Eine Taste, die schon beim Start gedrueckt ist, erzeugt
  // kein Ereignis und kann die Geste damit nicht ausloesen.
  if (!buttons_isDown(BTN_LEFT) || leftEditsValue || currentScreen == SCR_HALTED) {
    leftHoldStart = 0;
  } else {
    if (ev == BTN_LEFT && leftHoldStart == 0) leftHoldStart = millis();
    if (leftHoldStart != 0 && (millis() - leftHoldStart) >= HALT_HOLD_MS) {
      leftHoldStart = 0;
      doHalt();
      return;
    }
  }

  pollStatus();

  // --- Startbild ---
  if (currentScreen == SCR_SPLASH) {
    if (needsRedraw) { drawSplash(); needsRedraw = false; }
    // Bei gemeldetem Verdrahtungsfehler laenger stehen lassen, damit die
    // Meldung lesbar ist.
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

  // --- Bestaetigung nach NOT-HALT ---
  if (currentScreen == SCR_HALTED) {
    if (needsRedraw) { drawHalted(); needsRedraw = false; }
    // Bewusst jede Taste: eine einzelne klemmende Taste darf die Bedienung
    // nicht dauerhaft blockieren.
    if (ev != BTN_NONE) {
      currentScreen = SCR_PAGE;
      needsRedraw   = true;
    }
    return;
  }

  // --- Laufende Sequenz: Navigation gesperrt, nur ENTER haelt an ---
  // Greift auch, wenn der Lauf ueber die serielle Konsole gestartet wurde.
  if (sequence_isRunning()) {
    if (currentScreen != SCR_RUNNING) {
      currentScreen = SCR_RUNNING;
      needsRedraw   = true;
    }
    if (needsRedraw) { drawRunningStatic(); needsRedraw = false; }
    drawRunningDynamic();
    if (ev == BTN_ENTER) doHalt();
    return;
  }

  // Sequenz ist gerade fertig geworden -> zurueck ins Menue
  if (currentScreen == SCR_RUNNING) {
    currentScreen = SCR_PAGE;
    needsRedraw   = true;
  }

  // --- Menueseiten ---
  if (ev != BTN_NONE) handlePageInput(ev);

  // Kopfzeile nachfuehren, wenn sich Position oder Busy-Zustand geaendert hat
  if (!needsRedraw && !needsRows) {
    char now[sizeof(lastStatusText)];
    buildStatusText(now, sizeof(now));
    if (strcmp(now, lastStatusText) != 0) needsHeader = true;
  }

  if (needsRedraw) {
    drawPage();
    needsRedraw = false;
    needsRows   = false;
    needsHeader = false;
  } else if (needsRows) {
    drawRows();
    drawFooter(footerHint());
    drawScrollMark();
    needsRows = false;
  } else if (needsHeader) {
    drawHeader(PAGES[curPage].title ? PAGES[curPage].title : dynTitle);
    needsHeader = false;
  }
}
