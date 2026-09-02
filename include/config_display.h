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
//   Modul-Pin 5  RESET      -> GPIO16
//   Modul-Pin 6  CS         -> GPIO5    (VSPI CS)
//   Modul-Pin 7  GND        -> GND
//   Modul-Pin 8  VCC        -> +3V3
//
// Hinweis: Die I2C-Leitungen (GPIO21/22) bleiben unberuehrt, das Display haengt
// komplett am Hardware-SPI (VSPI) und stoert die Slave-Kommunikation nicht.
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

// SPI-Takt. ST7735 vertraegt in der Regel 26-40 MHz. Bei langen Kabeln
// oder Bildstoerungen auf 10000000 reduzieren.
#define TFT_SPI_HZ     26000000

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
