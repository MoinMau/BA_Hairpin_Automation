#include "display_ui.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#include "config_display.h"
#include "buttons.h"

// ----------------------------------------------------------------------------
// Display-Objekt.
//   Software-SPI: bitbang ueber MOSI/SCLK, ca. 1-2 MHz, sehr tolerant
//                 gegenueber langen Kabeln. Zur Fehlersuche die erste Wahl.
//   Hardware-SPI: VSPI, deutlich schneller, aber empfindlicher.
// ----------------------------------------------------------------------------
#if TFT_USE_SOFT_SPI
static Adafruit_ST7735 tft(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_MOSI,
                           TFT_PIN_SCLK, TFT_PIN_RST);
#else
static Adafruit_ST7735 tft(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);
#endif

// --- Farbschema (an einer Stelle definiert, damit spaeter leicht anpassbar) ---
#define COL_BG         ST77XX_BLACK
#define COL_TEXT       ST77XX_WHITE
#define COL_DIM        0x8410            // mittleres Grau (RGB565)
#define COL_HEADER_BG  0x001F            // Blau
#define COL_SEL_BG     0xFD20            // Orange
#define COL_SEL_TEXT   ST77XX_BLACK
#define COL_OK         ST77XX_GREEN
#define COL_WARN       ST77XX_RED

// --- Layout (Querformat: 160 x 128) ---
static int16_t scrW = 160;
static int16_t scrH = 128;
#define HEADER_H   14
#define FOOTER_H   12
#define ROW_H      14
#define PAD_X       4

// ----------------------------------------------------------------------------
// Screens der Teststufe
// ----------------------------------------------------------------------------
enum UiScreen {
  SCR_SPLASH,        // Startbild
  SCR_MENU,          // Hauptliste
  SCR_LAYOUT_TEST,   // Rahmen/Raster -> prueft Offsets und Bildgrenzen
  SCR_COLOR_TEST,    // Farbbalken    -> prueft Tab-Typ / Farbreihenfolge
  SCR_BUTTON_TEST,   // Live-Anzeige aller Tasten
  SCR_VALUE_DEMO,    // Wert mit LEFT/RIGHT aendern (Vorschau Parameter-Editor)
  SCR_INFO
};

static UiScreen currentScreen = SCR_SPLASH;
static unsigned long splashStart = 0;
#define SPLASH_MS 2500
static bool     needsRedraw   = true;   // Vollbild neu zeichnen
static bool     needsPartial  = false;  // nur veraenderliche Bereiche

// --- Hauptmenue ---
static const char* MENU_ITEMS[] = {
  "Layout-Test",
  "Farb-Test",
  "Tasten-Test",
  "Wert-Demo",
  "Info"
};
static const uint8_t MENU_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
static uint8_t menuIndex = 0;

// --- Zustand Wert-Demo ---
static int demoValue = 500;   // 0..1000, Schrittweite 10 (wie Servo-Stellwert)

// --- Zustand Tasten-Test ---
static ButtonId lastEvent      = BTN_NONE;
static uint32_t eventCounter   = 0;
static uint8_t  lastDrawnMask  = 0xFF;  // erzwingt ersten Redraw
static unsigned long enterHoldStart = 0; // fuer "ENTER halten = zurueck"

// ----------------------------------------------------------------------------
// Zeichen-Helfer
// ----------------------------------------------------------------------------

static void drawHeader(const char* title) {
  tft.fillRect(0, 0, scrW, HEADER_H, COL_HEADER_BG);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(1);
  tft.setCursor(PAD_X, 4);
  tft.print(title);
}

static void drawFooter(const char* hint) {
  tft.fillRect(0, scrH - FOOTER_H, scrW, FOOTER_H, COL_BG);
  tft.drawFastHLine(0, scrH - FOOTER_H, scrW, COL_DIM);
  tft.setTextColor(COL_DIM);
  tft.setTextSize(1);
  tft.setCursor(PAD_X, scrH - FOOTER_H + 3);
  tft.print(hint);
}

static void clearBody() {
  tft.fillRect(0, HEADER_H, scrW, scrH - HEADER_H - FOOTER_H, COL_BG);
}

// ----------------------------------------------------------------------------
// Screen: Startbild
// ----------------------------------------------------------------------------
static void drawSplash() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 34);
  tft.print("BA Hairpin");
  tft.setTextSize(1);
  tft.setTextColor(COL_SEL_BG);
  tft.setCursor(10, 58);
  tft.print("Display-Teststufe v0.1");
  tft.setTextColor(COL_DIM);
  tft.setCursor(10, 78);
  tft.print("Taste druecken...");
}

// ----------------------------------------------------------------------------
// Screen: Hauptmenue
// ----------------------------------------------------------------------------
static void drawMenu() {
  drawHeader("BA Hairpin  -  Menue");
  clearBody();

  for (uint8_t i = 0; i < MENU_COUNT; i++) {
    int16_t y = HEADER_H + 2 + i * ROW_H;
    bool sel = (i == menuIndex);

    if (sel) {
      tft.fillRect(2, y - 1, scrW - 4, ROW_H - 1, COL_SEL_BG);
      tft.setTextColor(COL_SEL_TEXT);
    } else {
      tft.setTextColor(COL_TEXT);
    }
    tft.setTextSize(1);
    tft.setCursor(PAD_X + 4, y + 3);
    tft.print(sel ? ">" : " ");
    tft.print(" ");
    tft.print(MENU_ITEMS[i]);
  }
  drawFooter("UP/DOWN  ENTER=oeffnen");
}

// ----------------------------------------------------------------------------
// Screen: Layout-Test
// Zeigt Rahmen, Ecken und Raster. Damit laesst sich pruefen, ob das Panel
// vollstaendig angesteuert wird oder ob ein Offset (falscher Tab-Typ) vorliegt.
// ----------------------------------------------------------------------------
static void drawLayoutTest() {
  tft.fillScreen(COL_BG);

  // Aeusserer Rahmen: muss exakt am Bildrand liegen
  tft.drawRect(0, 0, scrW, scrH, ST77XX_WHITE);
  tft.drawRect(1, 1, scrW - 2, scrH - 2, ST77XX_WHITE);

  // Eckmarker: alle vier muessen vollstaendig sichtbar sein
  tft.fillRect(2, 2, 8, 8, ST77XX_RED);
  tft.fillRect(scrW - 10, 2, 8, 8, ST77XX_GREEN);
  tft.fillRect(2, scrH - 10, 8, 8, ST77XX_BLUE);
  tft.fillRect(scrW - 10, scrH - 10, 8, 8, ST77XX_YELLOW);

  // Diagonalen zur Mittenkontrolle
  tft.drawLine(0, 0, scrW - 1, scrH - 1, COL_DIM);
  tft.drawLine(scrW - 1, 0, 0, scrH - 1, COL_DIM);

  tft.setTextColor(COL_TEXT);
  tft.setTextSize(1);
  tft.setCursor(16, 30);
  tft.print("Rahmen sichtbar?");
  tft.setCursor(16, 42);
  tft.print("4 Ecken sichtbar?");

  tft.setTextSize(2);
  tft.setCursor(16, 60);
  tft.print("160x128");

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(16, 86);
  tft.print("LEFT = zurueck");
}

// ----------------------------------------------------------------------------
// Screen: Farb-Test
// Farbbalken mit Beschriftung. Wenn Rot und Blau vertauscht erscheinen,
// muss in config_display.h ein anderer TFT_TAB_TYPE gewaehlt werden.
// ----------------------------------------------------------------------------
static void drawColorTest() {
  drawHeader("Farb-Test");
  clearBody();

  struct { uint16_t col; const char* name; } bars[] = {
    { ST77XX_RED,     "ROT"    },
    { ST77XX_GREEN,   "GRUEN"  },
    { ST77XX_BLUE,    "BLAU"   },
    { ST77XX_YELLOW,  "GELB"   },
    { ST77XX_CYAN,    "CYAN"   },
    { ST77XX_MAGENTA, "MAGENTA"}
  };
  const uint8_t n = sizeof(bars) / sizeof(bars[0]);
  int16_t top = HEADER_H + 2;
  int16_t h   = (scrH - HEADER_H - FOOTER_H - 4) / n;

  for (uint8_t i = 0; i < n; i++) {
    int16_t y = top + i * h;
    tft.fillRect(0, y, 90, h - 1, bars[i].col);
    tft.setTextColor(COL_TEXT);
    tft.setTextSize(1);
    tft.setCursor(96, y + (h - 8) / 2);
    tft.print(bars[i].name);
  }
  drawFooter("Farbe = Text? LEFT=zurueck");
}

// ----------------------------------------------------------------------------
// Screen: Tasten-Test
// Zeigt live, welche Taste gedrueckt ist. Deckt Verdrahtungsfehler auf.
// ----------------------------------------------------------------------------
static void drawButtonTestStatic() {
  drawHeader("Tasten-Test");
  clearBody();
  drawFooter("ENTER 1.5s halten = zurueck");
  lastDrawnMask = 0xFF;  // erzwingt Neuzeichnen der dynamischen Felder
}

static void drawButtonTestDynamic() {
  // Aktuelle Tastenmaske bilden
  uint8_t mask = 0;
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (buttons_isDown((ButtonId)i)) mask |= (1 << i);
  }
  if (mask == lastDrawnMask) return;   // nichts veraendert -> kein Flackern
  lastDrawnMask = mask;

  const char* labels[BTN_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "ENTER" };
  int16_t top = HEADER_H + 4;

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    int16_t y   = top + i * 15;
    bool    on  = (mask & (1 << i)) != 0;

    tft.fillRect(PAD_X, y, 60, 13, COL_BG);
    tft.setTextColor(on ? COL_OK : COL_DIM);
    tft.setTextSize(1);
    tft.setCursor(PAD_X, y + 3);
    tft.print(labels[i]);

    // Statusbalken rechts daneben
    tft.fillRect(70, y, 50, 12, on ? COL_OK : 0x2104);
  }

  // Zaehler der erkannten Ereignisse
  tft.fillRect(124, top, 34, 40, COL_BG);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(126, top + 2);
  tft.print("EVT");
  tft.setCursor(126, top + 14);
  tft.print(eventCounter);
}

// ----------------------------------------------------------------------------
// Screen: Wert-Demo
// Vorschau auf den spaeteren Parameter-Editor: LEFT/RIGHT aendert den Wert.
// ----------------------------------------------------------------------------
static void drawValueDemoStatic() {
  drawHeader("Wert-Demo");
  clearBody();
  tft.setTextColor(COL_DIM);
  tft.setTextSize(1);
  tft.setCursor(PAD_X, HEADER_H + 6);
  tft.print("Servo-Stellwert (0-1000)");
  drawFooter("LEFT/RIGHT +-10  ENTER=OK");
}

static void drawValueDemoDynamic() {
  // Zahl
  tft.fillRect(PAD_X, HEADER_H + 22, scrW - 2 * PAD_X, 24, COL_BG);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(3);
  tft.setCursor(PAD_X + 20, HEADER_H + 22);
  tft.print(demoValue);

  // Balken als visuelle Rueckmeldung
  int16_t barX = PAD_X, barY = HEADER_H + 56, barW = scrW - 2 * PAD_X, barH = 12;
  tft.drawRect(barX, barY, barW, barH, COL_DIM);
  int16_t fill = (int32_t)(barW - 2) * demoValue / 1000;
  tft.fillRect(barX + 1, barY + 1, fill, barH - 2, COL_SEL_BG);
  tft.fillRect(barX + 1 + fill, barY + 1, (barW - 2) - fill, barH - 2, COL_BG);
}

// ----------------------------------------------------------------------------
// Screen: Info
// ----------------------------------------------------------------------------
static void drawInfo() {
  drawHeader("Info");
  clearBody();
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT);

  int16_t y = HEADER_H + 4;
  tft.setCursor(PAD_X, y);      tft.print("BA Hairpin Automation");
  y += 12;
  tft.setTextColor(COL_DIM);
  tft.setCursor(PAD_X, y);      tft.print("UI-Teststufe v0.1");
  y += 14;
  tft.setTextColor(COL_TEXT);
  tft.setCursor(PAD_X, y);      tft.print("TFT : ST7735 128x160");
  y += 11;
  tft.setCursor(PAD_X, y);      tft.print("SPI : 18/23 CS5 DC4 RS16");
  y += 11;
  tft.setCursor(PAD_X, y);      tft.print("BTN : 32/33/25/26/27");
  y += 11;
  tft.setCursor(PAD_X, y);      tft.print("I2C : 21/22 (unberuehrt)");

  drawFooter("LEFT = zurueck");
}

// ----------------------------------------------------------------------------
// Navigation
// ----------------------------------------------------------------------------
static void openScreen(UiScreen s) {
  currentScreen = s;
  needsRedraw   = true;
  Serial.print(F("[UI] Screen -> ")); Serial.println((int)s);
}

static void handleMenuInput(ButtonId ev) {
  switch (ev) {
    case BTN_UP:
      menuIndex = (menuIndex == 0) ? (MENU_COUNT - 1) : (menuIndex - 1);
      needsRedraw = true;
      break;
    case BTN_DOWN:
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      needsRedraw = true;
      break;
    case BTN_RIGHT:
    case BTN_ENTER:
      switch (menuIndex) {
        case 0: openScreen(SCR_LAYOUT_TEST); break;
        case 1: openScreen(SCR_COLOR_TEST);  break;
        case 2: openScreen(SCR_BUTTON_TEST); break;
        case 3: openScreen(SCR_VALUE_DEMO);  break;
        case 4: openScreen(SCR_INFO);        break;
      }
      break;
    default:
      break;
  }
}

static void handleValueDemoInput(ButtonId ev) {
  if (ev == BTN_RIGHT) {
    demoValue = min(1000, demoValue + 10);
    needsPartial = true;
  } else if (ev == BTN_LEFT) {
    demoValue = max(0, demoValue - 10);
    needsPartial = true;
  } else if (ev == BTN_UP) {
    demoValue = min(1000, demoValue + 100);
    needsPartial = true;
  } else if (ev == BTN_DOWN) {
    demoValue = max(0, demoValue - 100);
    needsPartial = true;
  } else if (ev == BTN_ENTER) {
    Serial.print(F("[UI] Wert bestaetigt: ")); Serial.println(demoValue);
    openScreen(SCR_MENU);
  }
}

// ----------------------------------------------------------------------------
// Oeffentliche API
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Panel-Initialisierung
// ----------------------------------------------------------------------------
// Die Bibliothek fahert ihre Init-Sequenz fest mit 32 MHz (SPI_DEFAULT_FREQ in
// Adafruit_ST77xx.cpp) - das laesst sich von aussen nicht setzen. Bei langen
// Kabeln kommen diese Befehle verstuemmelt an und das Panel bleibt schwarz.
// Deshalb wird direkt danach auf TFT_SPI_HZ heruntergeschaltet und die
// entscheidenden Einschaltbefehle werden noch einmal gesendet.
//
// Die delay() hier sind Datenblatt-Wartezeiten des ST7735 und laufen
// ausschliesslich einmalig in setup(), bevor die Maschine arbeitet. Die
// Regel "kein delay() in der Ablaufsteuerung" bleibt davon unberuehrt.
static void tftInitPanel(uint8_t tabType) {
  tft.initR(tabType);            // 32 MHz, kann bei langen Kabeln scheitern
  tft.setSPISpeed(TFT_SPI_HZ);   // ab hier sicherer Takt

  // Einschaltbefehle nachreichen - jetzt langsam und damit zuverlaessig
  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(150);
  uint8_t colmod = 0x05;           // 16 Bit pro Pixel (RGB565)
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(100);

  tft.setRotation(TFT_ROTATION);   // sendet MADCTL erneut
#if TFT_INVERT_COLORS
  tft.invertDisplay(true);
#endif
  scrW = tft.width();
  scrH = tft.height();
}

#if TFT_DIAG_MODE
// ============================================================================
// DIAGNOSE-MODUS
// ----------------------------------------------------------------------------
// Zeigt nacheinander Vollbildfarben und probiert dabei automatisch alle
// Panel-Varianten durch. Der Serial-Monitor schreibt mit, was gerade auf dem
// Schirm stehen muesste - so laesst sich "gar kein Bild" von "falsche
// Variante" unterscheiden, ohne jedes Mal neu zu flashen.
// ============================================================================

struct DiagStep { uint16_t color; const char* name; };
static const DiagStep DIAG_STEPS[] = {
  { ST77XX_RED,   "ROT"     },
  { ST77XX_GREEN, "GRUEN"   },
  { ST77XX_BLUE,  "BLAU"    },
  { ST77XX_WHITE, "WEISS"   },
  { ST77XX_BLACK, "SCHWARZ + Text" }
};
static const uint8_t DIAG_STEP_COUNT = sizeof(DIAG_STEPS) / sizeof(DIAG_STEPS[0]);

struct DiagTab { uint8_t tab; const char* name; };
static const DiagTab DIAG_TABS[] = {
  { INITR_BLACKTAB,     "BLACKTAB"     },
  { INITR_GREENTAB,     "GREENTAB"     },
  { INITR_REDTAB,       "REDTAB"       },
  { INITR_144GREENTAB,  "144GREENTAB"  }
};
static const uint8_t DIAG_TAB_COUNT = sizeof(DIAG_TABS) / sizeof(DIAG_TABS[0]);

#define DIAG_STEP_MS 1800

static uint8_t       diagStep     = 0;
static uint8_t       diagTab      = 0;
static unsigned long diagLast     = 0;
static bool          diagFirst    = true;
static unsigned long diagLedLast  = 0;
static bool          diagLedState = false;

static void diagDrawStep() {
  const DiagStep& st = DIAG_STEPS[diagStep];
  tft.fillScreen(st.color);

  if (st.color == ST77XX_BLACK) {
    // Textbild: prueft zusaetzlich Rahmen und Bildgrenzen
    tft.drawRect(0, 0, scrW, scrH, ST77XX_WHITE);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(6, 10);  tft.print("DIAGNOSE");
    tft.setCursor(6, 26);  tft.print(DIAG_TABS[diagTab].name);
    tft.setCursor(6, 42);  tft.print(scrW); tft.print("x"); tft.print(scrH);
    tft.setCursor(6, 58);
#if TFT_USE_SOFT_SPI
    tft.print("SOFT-SPI");
#else
    tft.print("HW-SPI ");
    tft.print(TFT_SPI_HZ / 1000000); tft.print("MHz");
#endif
  }

  Serial.print(F("[DIAG] Variante "));
  Serial.print(DIAG_TABS[diagTab].name);
  Serial.print(F("  ->  Bildschirm sollte jetzt sein: "));
  Serial.println(st.name);
}

static void diagUpdate() {
  unsigned long now = millis();

  // Heartbeat auf der Onboard-LED: zeigt, dass die Firmware wirklich laeuft
  if (now - diagLedLast >= 500) {
    diagLedLast  = now;
    diagLedState = !diagLedState;
    digitalWrite(2, diagLedState ? HIGH : LOW);
  }

#if TFT_BL_CONTROLLED
  // Backlight im Takt der Farbwechsel schalten - damit ist erkennbar, ob das
  // Panel ueberhaupt versorgt wird, selbst wenn der Controller nicht antwortet.
  digitalWrite(TFT_PIN_BL, HIGH);
#endif

  if (!diagFirst && (now - diagLast) < DIAG_STEP_MS) return;
  diagFirst = false;
  diagLast  = now;

  diagDrawStep();

  diagStep++;
  if (diagStep >= DIAG_STEP_COUNT) {
    diagStep = 0;
    diagTab  = (diagTab + 1) % DIAG_TAB_COUNT;
    Serial.print(F("[DIAG] --- wechsle Panel-Variante auf "));
    Serial.print(DIAG_TABS[diagTab].name);
    Serial.println(F(" ---"));
    tftInitPanel(DIAG_TABS[diagTab].tab);
  }
}
#endif // TFT_DIAG_MODE

void ui_begin() {
  buttons_begin();

#if TFT_BL_CONTROLLED
  pinMode(TFT_PIN_BL, OUTPUT);
  digitalWrite(TFT_PIN_BL, HIGH);
#endif

#if !TFT_USE_SOFT_SPI
  // VSPI explizit auf die verdrahteten Pins legen (MISO wird nicht benutzt).
  SPI.begin(TFT_PIN_SCLK, -1, TFT_PIN_MOSI, TFT_PIN_CS);
#endif

  Serial.println(F("\n[UI] Display-Init startet..."));
#if TFT_USE_SOFT_SPI
  Serial.println(F("[UI] Modus: SOFTWARE-SPI (bitbang, robust)"));
#else
  Serial.print(F("[UI] Modus: HARDWARE-SPI @ "));
  Serial.print(TFT_SPI_HZ / 1000000); Serial.println(F(" MHz"));
#endif
  Serial.print(F("[UI] Pins  : SCLK=")); Serial.print(TFT_PIN_SCLK);
  Serial.print(F(" MOSI="));  Serial.print(TFT_PIN_MOSI);
  Serial.print(F(" CS="));    Serial.print(TFT_PIN_CS);
  Serial.print(F(" DC="));    Serial.print(TFT_PIN_DC);
  Serial.print(F(" RST="));   Serial.println(TFT_PIN_RST);

#if TFT_DIAG_MODE
  tftInitPanel(DIAG_TABS[0].tab);
  Serial.println(F("[UI] DIAGNOSE-MODUS aktiv - Menue ist deaktiviert."));
#else
  tftInitPanel(TFT_TAB_TYPE);
  currentScreen = SCR_SPLASH;
  splashStart   = millis();
  needsRedraw   = true;
#endif

  Serial.print(F("[UI] Display bereit: "));
  Serial.print(scrW); Serial.print('x'); Serial.println(scrH);
}

void ui_update() {
#if TFT_DIAG_MODE
  diagUpdate();
  // Tasten trotzdem einlesen: die Ereignisse landen im Serial-Monitor und
  // lassen sich so auch ohne Bild pruefen.
  ButtonId dev = buttons_update();
  if (dev != BTN_NONE) {
    Serial.print(F("[BTN] ")); Serial.println(buttons_name(dev));
  }
  return;
#else
  ButtonId ev = buttons_update();

  if (ev != BTN_NONE) {
    lastEvent = ev;
    eventCounter++;
    Serial.print(F("[BTN] ")); Serial.println(buttons_name(ev));
  }

  // Startbild: endet nach Ablauf der Zeit oder beim ersten Tastendruck.
  if (currentScreen == SCR_SPLASH) {
    if (needsRedraw) { drawSplash(); needsRedraw = false; }
    if (ev != BTN_NONE || (millis() - splashStart) >= SPLASH_MS) {
      openScreen(SCR_MENU);
    }
    return;
  }

  // Aus den Anzeige-Screens fuehrt LEFT zurueck ins Hauptmenue.
  // Ausgenommen: Wert-Demo (dort ist LEFT die Minus-Taste) und Tasten-Test
  // (dort muss LEFT selbst pruefbar bleiben -> Ausstieg per ENTER halten).
  if (ev == BTN_LEFT && currentScreen != SCR_VALUE_DEMO && currentScreen != SCR_BUTTON_TEST) {
    openScreen(SCR_MENU);
    ev = BTN_NONE;
  }

  switch (currentScreen) {
    case SCR_SPLASH:
      break;

    case SCR_MENU:
      if (ev != BTN_NONE) handleMenuInput(ev);
      if (needsRedraw) { drawMenu(); needsRedraw = false; }
      break;

    case SCR_LAYOUT_TEST:
      if (needsRedraw) { drawLayoutTest(); needsRedraw = false; }
      break;

    case SCR_COLOR_TEST:
      if (needsRedraw) { drawColorTest(); needsRedraw = false; }
      break;

    case SCR_BUTTON_TEST:
      if (needsRedraw) {
        drawButtonTestStatic();
        needsRedraw    = false;
        enterHoldStart = 0;
      }
      drawButtonTestDynamic();   // zeichnet nur bei Aenderung

      // Ausstieg: ENTER 1.5 s halten. So bleiben alle fuenf Tasten pruefbar.
      if (buttons_isDown(BTN_ENTER)) {
        if (enterHoldStart == 0) enterHoldStart = millis();
        else if (millis() - enterHoldStart >= 1500) openScreen(SCR_MENU);
      } else {
        enterHoldStart = 0;
      }
      break;

    case SCR_VALUE_DEMO:
      if (ev != BTN_NONE) handleValueDemoInput(ev);
      if (needsRedraw) {
        drawValueDemoStatic();
        drawValueDemoDynamic();
        needsRedraw  = false;
        needsPartial = false;
      } else if (needsPartial) {
        drawValueDemoDynamic();
        needsPartial = false;
      }
      break;

    case SCR_INFO:
      if (needsRedraw) { drawInfo(); needsRedraw = false; }
      break;
  }
#endif // TFT_DIAG_MODE
}
