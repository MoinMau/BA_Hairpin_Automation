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
// Querformat -> mehr Platz fuer Menuezeilen. 3 = um 180 Grad gedreht
// gegenueber 1 (Einbaulage 02.09.2026).
#define TFT_ROTATION    3

// SPI-Takt fuers Zeichnen. Bewusst niedrig gesetzt: bei langen Dupont-Kabeln
// ist die Flankensteilheit das Problem, nicht die Rechenleistung.
// Wenn das Bild sauber steht, schrittweise erhoehen: 8M -> 16M -> 26M.
#define TFT_SPI_HZ      4000000

// Software-SPI (Bitbang) statt Hardware-SPI.
//
// Hintergrund: Die Adafruit-Bibliothek faehrt ihre Init-Sequenz immer mit fest
// einkompilierten 32 MHz (Adafruit_ST77xx.cpp: SPI_DEFAULT_FREQ); das laesst
// sich von aussen nicht setzen. Bei langen Kabeln kommen die Init-Befehle
// dadurch verstuemmelt an und das Panel bleibt schwarz. tftInitPanel() faengt
// das ab, indem es nach initR() auf TFT_SPI_HZ umschaltet und die
// Einschaltbefehle nochmals sendet.
//
// 1 = Bitbang, langsamer aber unempfindlich gegen lange Leitungen (aktuell).
// 0 = Hardware-SPI (VSPI), fluessigerer Bildaufbau.
#define TFT_USE_SOFT_SPI    1

// --- Panel-Variante ("Tab") ---
// Fuer das verbaute Modul verifiziert: INITR_BLACKTAB.
// Alternativen bei verschobenem Bild oder vertauschten Farben:
// INITR_REDTAB, INITR_GREENTAB.
#define TFT_TAB_TYPE   INITR_BLACKTAB

// Farben invertiert? Manche V1.2-Chargen brauchen invertDisplay(true).
#define TFT_INVERT_COLORS  0

// ============================================================================
// Konfiguration: Bedientasten (5-Wege-Navigation)
// ----------------------------------------------------------------------------
//
//   UP     -> GPIO27
//   DOWN   -> GPIO32
//   LEFT   -> GPIO33
//   RIGHT  -> GPIO26
//   ENTER  -> GPIO25
//
// Belegung laut Verdrahtung vom 02.09.2026.
//
// SCHALTUNG: Die verbauten Taster haben einen festen Widerstand von rund
// 5 kOhm zwischen Signal und GND, parallel zum Kontakt. Gegen den internen
// Pullup (rund 45 kOhm) ergibt das im Ruhezustand nur 0,33 V - der Eingang
// laege dauerhaft auf LOW und ein Tastendruck waere nicht erkennbar.
//
// Deshalb ist die gemeinsame Schiene der Taster auf +3V3 gelegt statt auf GND.
// Der 5-kOhm-Widerstand wirkt damit als Pulldown:
//
//     Ruhe      : Pin ueber 5 kOhm nach GND      -> LOW
//     Gedrueckt : Pin direkt auf +3V3            -> HIGH
//
// Zusaetzlich ist der interne Pulldown aktiv. Bricht ein Draht, liest der
// Eingang weiterhin LOW (nicht gedrueckt) statt zu floaten.
// Keiner der fuenf GPIOs ist ein Strapping-Pin, ein HIGH beim Booten ist
// also unkritisch.
//
// Diese GPIOs sind bewusst gewaehlt: keine Strapping-Pins, interne Pullups
// vorhanden, kein Konflikt mit SPI (18/23/4/16/5) oder I2C (21/22).
// ============================================================================

// Taster gegen +3V3 (1) oder gegen GND (0). Siehe Erlaeuterung oben.
#define BTN_ACTIVE_HIGH  1

#define BTN_PIN_UP     27
#define BTN_PIN_DOWN   32
#define BTN_PIN_LEFT   33
#define BTN_PIN_RIGHT  26
#define BTN_PIN_ENTER  25

// Entprellzeit in ms
#define BTN_DEBOUNCE_MS      30
// Wartezeit bis die Autorepeat-Funktion einsetzt (Taste halten)
#define BTN_REPEAT_DELAY_MS  450
// Intervall der Autorepeat-Wiederholungen
#define BTN_REPEAT_RATE_MS   120

#endif // CONFIG_DISPLAY_H
