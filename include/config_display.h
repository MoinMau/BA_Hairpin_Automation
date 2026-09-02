#ifndef CONFIG_DISPLAY_H
#define CONFIG_DISPLAY_H

// ============================================================================
// Konfiguration: 1.8" TFT SPI Modul (ST7735, 128x160, V1.2) am ESP32-WROOM-32
// ----------------------------------------------------------------------------
// Verdrahtung laut Modul-Beschriftung:
//   Modul-Pin 1  LED / BL   -> 3V3 ueber 47 Ohm  ODER  GPIO17 (dimmbar)
//   Modul-Pin 2  SCK        -> GPIO18   (VSPI CLK)
//   Modul-Pin 3  SDA (MOSI) -> GPIO23   (VSPI MOSI)
//   Modul-Pin 4  A0  (DC)   -> GPIO4
//   Modul-Pin 5  RESET      -> GPIO16   (Achtung bei ESP32-WROVER, s.u.)
//   Modul-Pin 6  CS         -> GPIO5    (VSPI CS)
//   Modul-Pin 7  GND        -> GND
//   Modul-Pin 8  VCC        -> +3V3
//
// Hinweis: Die I2C-Leitungen (GPIO21/22) bleiben unberuehrt, das Display haengt
// komplett am SPI und stoert die Slave-Kommunikation nicht.
//
// ACHTUNG WROVER: Auf ESP32-WROVER-Modulen sind GPIO16 und GPIO17 fest fuer das
// externe PSRAM verdrahtet und als GPIO unbrauchbar. Nur auf WROOM-32 (ohne
// PSRAM) sind sie frei. Im Zweifel RESET auf GPIO15 legen und TFT_PIN_RST
// entsprechend aendern.
// ============================================================================

// --- Display-Pins ---
#define TFT_PIN_SCLK   18
#define TFT_PIN_MOSI   23
#define TFT_PIN_DC      4
#define TFT_PIN_RST    16
#define TFT_PIN_CS      5
#define TFT_PIN_BL     17   // Backlight; nur genutzt wenn TFT_BL_CONTROLLED = 1

// Backlight ueber GPIO17 schalten? 0 = LED-Pin fest an 3V3 (ueber 47 Ohm)
#define TFT_BL_CONTROLLED   0

// --- Panel-Geometrie ---
#define TFT_WIDTH_PX   128
#define TFT_HEIGHT_PX  160

// Rotation: 0/2 = Hochformat (128x160), 1/3 = Querformat (160x128)
// Fuer die Menuefuehrung nutzen wir Querformat -> mehr Platz fuer Zeilen.
#define TFT_ROTATION    1

// SPI-Takt fuers Zeichnen. Bewusst niedrig gesetzt: bei langen Dupont-Kabeln
// ist die Flankensteilheit das Problem, nicht die Rechenleistung.
// Wenn das Bild sauber steht, schrittweise erhoehen: 8M -> 16M -> 26M.
#define TFT_SPI_HZ      4000000

// ----------------------------------------------------------------------------
// WICHTIG: Die Adafruit-Bibliothek fuehrt ihre Init-Sequenz IMMER mit fest
// einkompilierten 32 MHz aus (Adafruit_ST77xx.cpp: SPI_DEFAULT_FREQ). Das laesst
// sich von aussen nicht setzen. Bei langen Kabeln kommen die Init-Befehle
// dadurch verstuemmelt an und das Panel bleibt schwarz.
// Gegenmassnahmen (in dieser Reihenfolge probieren):
//   1) TFT_USE_SOFT_SPI = 1  -> Bitbang-SPI, langsam aber extrem robust.
//      Damit klaert sich, ob die Verdrahtung stimmt.
//   2) TFT_USE_SOFT_SPI = 0  -> Hardware-SPI. ui_begin() sendet die
//      entscheidenden Init-Befehle nach dem Umschalten auf TFT_SPI_HZ
//      nochmals nach, damit ein misslungener 32-MHz-Init aufgefangen wird.
// ----------------------------------------------------------------------------
#define TFT_USE_SOFT_SPI    1

// Diagnose-Modus: statt des Menues laeuft ein Testbild-Durchlauf, der
// automatisch alle Panel-Varianten durchprobiert und im Serial-Monitor
// mitschreibt, was gerade auf dem Schirm stehen muesste.
// Auf 0 setzen, sobald ein Bild da ist.
// 02.09.2026: Bild laeuft mit BLACKTAB + Software-SPI -> Diagnose aus.
#define TFT_DIAG_MODE       0

// --- Panel-Variante ("Tab") ---
// Das rote 1.8"-Modul V1.2 laeuft fast immer mit INITR_BLACKTAB.
// Falls die Farben vertauscht sind (Rot <-> Blau) oder ein Rand sichtbar ist:
//   INITR_BLACKTAB  -> Standard, keine Offsets
//   INITR_REDTAB    -> falls ein weisser Rand oben/links sichtbar ist
//   INITR_GREENTAB  -> falls das Bild um 2/3 Pixel verschoben ist
// Umschalten ueber diesen Define, dann neu flashen.
#define TFT_TAB_TYPE   INITR_BLACKTAB

// Farben invertiert? Manche V1.2-Chargen brauchen invertDisplay(true).
#define TFT_INVERT_COLORS  0

// ============================================================================
// Konfiguration: Bedientasten (5-Wege-Navigation)
// ----------------------------------------------------------------------------
// Alle Taster schalten gegen GND, interne Pullups sind aktiv
// -> gedrueckt = LOW. Kein externer Widerstand noetig.
//
//   UP     -> GPIO32
//   DOWN   -> GPIO33
//   LEFT   -> GPIO25
//   RIGHT  -> GPIO26
//   ENTER  -> GPIO27
//
// Diese GPIOs sind bewusst gewaehlt: keine Strapping-Pins, interne Pullups
// vorhanden, kein Konflikt mit SPI (18/23/4/16/5) oder I2C (21/22).
// ============================================================================

#define BTN_PIN_UP     32
#define BTN_PIN_DOWN   33
#define BTN_PIN_LEFT   25
#define BTN_PIN_RIGHT  26
#define BTN_PIN_ENTER  27

// Entprellzeit in ms
#define BTN_DEBOUNCE_MS      30
// Wartezeit bis die Autorepeat-Funktion einsetzt (Taste halten)
#define BTN_REPEAT_DELAY_MS  450
// Intervall der Autorepeat-Wiederholungen
#define BTN_REPEAT_RATE_MS   120

#endif // CONFIG_DISPLAY_H
