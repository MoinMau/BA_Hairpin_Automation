# Plan: TFT-Menuefuehrung fuer die Hairpin-Vereinzelung

Stand: 02.09.2026 · Branch `Otto_Display_Menu` (baut auf `Otto_BA_Hairpin_Automation` auf)

---

## 1. Hardware-Setup

### 1.1 Display — 1.8" TFT SPI, ST7735, 128x160, V1.2

| Modul-Pin | Signal | ESP32-WROOM-32 | Begruendung |
| :-- | :-- | :-- | :-- |
| 1 | LED / BL | 3V3 ueber 47 Ω *oder* GPIO17 | GPIO17 nur noetig, wenn gedimmt/abgeschaltet werden soll |
| 2 | SCK | **GPIO18** | VSPI-Standard-Takt |
| 3 | SDA (MOSI) | **GPIO23** | VSPI-Standard-Datenleitung |
| 4 | A0 / DC | **GPIO4** | frei, kein Strapping-Pin |
| 5 | RESET | **GPIO16** | frei |
| 6 | CS | **GPIO5** | VSPI-Standard-CS (Strapping-Pin, aber als Ausgang unkritisch) |
| 7 | GND | GND | |
| 8 | VCC | +3V3 | |

Das Display haengt am **VSPI**, die Slaves am **I2C (GPIO21/22)**. Zwei getrennte
Busse, also keine gegenseitige Beeinflussung — das ist die wichtigste Eigenschaft
dieses Setups: die Menue-Ausgabe kann die Motorsteuerung nicht ausbremsen.

### 1.2 Bedientasten — 5-Wege-Navigation

| Taste | GPIO | Beschaltung |
| :-- | :-- | :-- |
| UP | 32 | Taster gegen GND, interner Pullup |
| DOWN | 33 | " |
| LEFT | 25 | " |
| RIGHT | 26 | " |
| ENTER | 27 | " |

Bewusst gewaehlt: keine Strapping-Pins (0, 2, 12, 15), keine Input-Only-Pins
(34-39, die haben keine internen Pullups), kein Konflikt mit SPI oder I2C.
Externe Widerstaende sind nicht noetig.

**Entprellung:** 30 ms in Software, zusaetzlich Autorepeat (450 ms Verzoegerung,
danach alle 120 ms) fuer UP/DOWN/LEFT/RIGHT. ENTER hat bewusst *kein* Autorepeat,
damit ein zu langer Druck keine Aktion doppelt ausloest.

### 1.3 Software-Stack

`Adafruit GFX` + `Adafruit ST7735` statt `TFT_eSPI`. Grund: die gesamte
Konfiguration liegt in `include/config_display.h` und damit im Repository —
bei TFT_eSPI muesste man Dateien in der Library selbst patchen oder ein Dutzend
Build-Flags pflegen, was auf einem anderen Rechner erfahrungsgemaess schiefgeht.
Fuer ein Listenmenue ist die Adafruit-Bibliothek schnell genug.

---

## 2. Was in dieser Stufe schon implementiert ist (Teststufe v0.1)

Ziel dieser Stufe ist **ausschliesslich die Hardware-Verifikation**. Es wird noch
kein einziges I2C-Kommando aus dem Menue heraus gesendet.

| Datei | Inhalt |
| :-- | :-- |
| `include/config_display.h` | Alle Pins, Panel-Variante, SPI-Takt, Tastenparameter |
| `src_esp32/buttons.h/.cpp` | Entprellung + Autorepeat, liefert Tasten-*Ereignisse* |
| `src_esp32/display_ui.h/.cpp` | Testmenue mit fuenf Screens |
| `src_esp32/main.cpp` | nur drei Zeilen ergaenzt: Include, `ui_begin()`, `ui_update()` |

**Screens der Teststufe**

1. **Startbild** — 2,5 s oder bis zum ersten Tastendruck.
2. **Hauptmenue** — fuenf Eintraege, Auswahlbalken, Wrap-around.
3. **Layout-Test** — Rahmen, vier Eckmarker, Diagonalen. Damit laesst sich pruefen,
   ob das Panel vollstaendig angesteuert wird oder ob ein Offset vorliegt.
4. **Farb-Test** — sechs beschriftete Farbbalken. Stimmen Text und Farbe nicht
   ueberein, ist der Tab-Typ falsch (siehe Abschnitt 5).
5. **Tasten-Test** — Live-Anzeige aller fuenf Tasten plus Ereigniszaehler.
   Ausstieg per **ENTER 1,5 s halten**, damit alle fuenf Tasten pruefbar bleiben.
6. **Wert-Demo** — LEFT/RIGHT ±10, UP/DOWN ±100, mit Balkenanzeige. Das ist der
   Prototyp des spaeteren Parameter-Editors.
7. **Info** — Pinbelegung zum Abgleich mit der Verdrahtung.

Alles nicht-blockierend, `delay()` wird nirgends verwendet. Neu gezeichnet wird
nur bei Aenderung (`needsRedraw` / `needsPartial`), deshalb flackert nichts.

---

## 3. Geplante Menuestruktur (Stufe 2)

```
[Statuszeile: Programm | Zustand | X/Y/Z busy | I2C ok]
│
├─ 1 Programme
│   ├─ Programm 1 (Hairpin)      → Durchlaeufe: [ 1 ]  → START
│   ├─ Programm 2                → Durchlaeufe: [ 1 ]  → START
│   ├─ Programm 3 (Homing 1x)    → Durchlaeufe: [ 1 ]  → START
│   └─ STOP (Sequenz abbrechen)
│
├─ 2 Manuell
│   ├─ Achse X ─┐
│   ├─ Achse Y ─┼→ Homing / Jog ± / Absolut anfahren / Stop
│   ├─ Achse Z ─┘
│   ├─ Servos   → S0…S5, Stellwert 0-1000 per LEFT/RIGHT
│   └─ Treiber  → EIN / AUS (enAll / disAll)
│
├─ 3 Status
│   ├─ Positionen  (X/Y/Z in Steps, Busy-Flags, Homing)
│   ├─ Servos      (sechs Stellwerte)
│   ├─ Sensoren    (Shunt A0, MH D2/A7, A1-A3 in Volt)
│   └─ I2C-Scan    (findet 0x32 / 0x33?)
│
├─ 4 Parameter
│   ├─ Vibration   (Amplitude, Frequenz)
│   ├─ Geschwind.  (Y-Speed, Z-Speed)
│   ├─ Wege        (Y-Vorschub, Z-Zustellung)
│   └─ Zeiten      (Servo-Settling, Vereinzelungsdauer)
│
└─ 5 System
    ├─ Display-Tests (Layout / Farbe / Tasten aus v0.1)
    ├─ Werte zuruecksetzen
    └─ Info
```

### Bedienlogik (durchgaengig gleich)

| Taste | Liste | Wert-Editor | Laufende Sequenz |
| :-- | :-- | :-- | :-- |
| UP / DOWN | Eintrag wechseln | grosse Schrittweite | — |
| LEFT | eine Ebene zurueck | Wert − | — |
| RIGHT | Ebene oeffnen | Wert + | — |
| ENTER | oeffnen / ausloesen | uebernehmen | — |
| LEFT 1,5 s halten | — | verwerfen | **NOT-STOP** |

Der Not-Stop ueber „LEFT halten" ist bewusst auf eine Halte-Geste gelegt: ein
versehentlicher kurzer Druck darf eine laufende Vereinzelung nicht abbrechen.
Er ruft dieselbe Logik wie das serielle `stop` auf (`axis_stop` auf X/Y/Z,
`currentSeqState = SEQ_IDLE`).

### Laufbildschirm

Solange `currentSeqState != SEQ_IDLE` schaltet die UI automatisch auf einen
Laufbildschirm: Programmnummer, aktueller Schritt im Klartext, verbleibende
Durchlaeufe, Fortschrittsbalken. Navigation ist dort gesperrt — nur der
Not-Stop bleibt aktiv. Damit kann man waehrend eines Laufs nichts verstellen.

---

## 4. Umsetzung in Stufe 2 — Schritte

**Schritt 1 — Maschinen-API herausloesen.**
`main.cpp` hat inzwischen 1188 Zeilen und mischt API, State Machine und
Serial-Parser. Die High-Level-Funktionen (`axis_*`, `servo_set`, `get_stepper_status`,
…) wandern nach `src_esp32/machine_api.h/.cpp`, die Sequenz nach
`src_esp32/sequence.h/.cpp`. Danach koennen Menue *und* Serial-Konsole dieselbe
API aufrufen, ohne dass eine der beiden Bedienarten die andere kennt.

**Schritt 2 — Status-Cache einfuehren.**
`is_axis_busy()` loest bei *jedem* Aufruf eine I2C-Transaktion aus. Wenn das Menue
zusaetzlich pollt, verdoppelt sich die Buslast. Deshalb: ein
`machine_pollStatus()`, das hoechstens alle 200 ms liest, und Menue wie
Sequenzsteuerung lesen anschliessend nur noch den Cache.

**Schritt 3 — Generisches Menue-Modell.**
Statt pro Screen eine eigene Zeichenfunktion ein Datenmodell:

```cpp
enum ItemType { IT_SUBMENU, IT_ACTION, IT_INT, IT_BOOL, IT_MONITOR };

struct MenuItem {
  const char* label;
  ItemType    type;
  const MenuList* sub;      // IT_SUBMENU
  void      (*action)();    // IT_ACTION
  int32_t*    value;        // IT_INT / IT_BOOL
  int32_t     min, max, step;
};
```

Ein Renderer plus ein Navigationsstack (max. 4 Ebenen) genuegen dann fuer den
gesamten Baum. Neue Menuepunkte sind danach eine Tabellenzeile, kein neuer Code.

**Schritt 4 — Parameter persistent machen.**
Werte aus „4 Parameter" in den NVS des ESP32 (`Preferences`-Library) schreiben,
damit sie einen Neustart ueberstehen. Beim Start laden, bei ENTER speichern.

**Schritt 5 — Sequenz auf Parameter umstellen.**
Die heute fest kodierten Werte in `updateSequence()` (z. B. `axis_rel(AXIS_Y, 2000, 800)`,
`18000` ms Vereinzelungsdauer, `axis_vibrate(AXIS_Z, 1, 50)`) durch die
Parameter-Variablen ersetzen. Erst dann hat das Menue echten Nutzen.

**Schritt 6 — Laufbildschirm.**
`updateSequence()` schreibt eine Klartext-Schrittbeschreibung in eine Variable,
die die UI anzeigt. Damit entfaellt der Zwang, am Serial-Monitor zu haengen.

---

## 5. Was du beim ersten Test pruefen solltest

Nach `pio run -e esp32 -t upload`:

1. **Kommt ein Bild?** Falls schwarz: Backlight (Modul-Pin 1) und die 3V3-Versorgung
   pruefen. Viele Module ziehen fuer die LED mehr Strom, als der ESP32-Pin liefert —
   deshalb ist die Variante „3V3 ueber 47 Ω" die sicherere.
2. **Layout-Test:** Sind alle vier Eckmarker vollstaendig sichtbar und liegt der
   Rahmen am Rand? Falls ein weisser Streifen oder eine Verschiebung sichtbar ist,
   in `config_display.h` `TFT_TAB_TYPE` auf `INITR_REDTAB` bzw. `INITR_GREENTAB`
   umstellen.
3. **Farb-Test:** Steht neben dem roten Balken auch „ROT"? Falls Rot und Blau
   vertauscht sind, ebenfalls der Tab-Typ. Falls alles wie ein Negativ aussieht,
   `TFT_INVERT_COLORS` auf `1` setzen.
4. **Tasten-Test:** Reagiert jede Taste einzeln und an der richtigen Stelle?
   Der Ereigniszaehler zeigt, ob eine Taste prellt (springt beim einmaligen
   Druecken um mehr als 1 → `BTN_DEBOUNCE_MS` erhoehen).
5. **Bildstoerungen?** Dann `TFT_SPI_HZ` auf `10000000` reduzieren — typisch bei
   langen Dupont-Kabeln.
6. **Ausrichtung:** Aktuell Querformat (`TFT_ROTATION 1`, 160x128). Wenn das
   Display anders eingebaut wird, auf `3` (um 180° gedreht) oder `0`/`2`
   (Hochformat) stellen. Das Layout rechnet mit `tft.width()/height()` und passt
   sich an.

Gib mir zu jedem Punkt kurz Rueckmeldung, dann gehen wir Stufe 2 an.

---

## 5a. Bekanntes Problem: 32-MHz-Init der Bibliothek

`Adafruit_ST7735::initR()` ruft intern `begin(0)` auf, und dort wird die
Frequenz auf `SPI_DEFAULT_FREQ = 32000000` gesetzt (`Adafruit_ST77xx.cpp:35`).
Die **gesamte Init-Sequenz laeuft also mit 32 MHz**, unabhaengig davon, was
`setSPISpeed()` sagt — das wirkt erst auf die nachfolgenden Zeichenbefehle.
Bei langen Dupont-Kabeln kommen die Init-Befehle verstuemmelt an, das Panel
wird nie eingeschaltet und bleibt schwarz.

Da `begin()` nicht virtuell ist, laesst sich das nicht sauber ueberschreiben.
Gegenmassnahmen in `tftInitPanel()`:

1. `TFT_USE_SOFT_SPI = 1` — Bitbang-SPI ueber den Konstruktor
   `Adafruit_ST7735(cs, dc, mosi, sclk, rst)`. Langsam, aber unempfindlich
   gegen lange Leitungen. Damit klaert sich, ob die Verdrahtung stimmt.
2. Nach `initR()` wird auf `TFT_SPI_HZ` heruntergeschaltet und die
   entscheidenden Einschaltbefehle (`SWRESET`, `SLPOUT`, `COLMOD`, `NORON`,
   `DISPON`) werden bei diesem sicheren Takt noch einmal gesendet. Ein
   misslungener 32-MHz-Init wird dadurch aufgefangen.

Mit `TFT_DIAG_MODE = 1` laeuft statt des Menues ein Testbild-Durchlauf, der
alle Panel-Varianten automatisch durchprobiert und im Serial-Monitor
protokolliert, was gerade zu sehen sein muesste.

---

## 6. Zwei Randnotizen zum bestehenden Code

Beim Einlesen sind mir zwei Stellen aufgefallen, die nichts mit dem Display zu
tun haben, aber vor Stufe 5 (Sequenz auf Parameter umstellen) angefasst werden
sollten:

- `src_esp32/main.cpp:748` — `case P2_POSITION_Y_WAIT:` hat kein `break;` und
  faellt in `P2_SERVO_INIT` durch. Die Servos werden dadurch schon gesetzt,
  waehrend Y noch faehrt.
- `src_esp32/main.cpp:704` — beim Wiederholen springt `P1_DONE` auf
  `(currentProgram == 1) ? P1_HOME_Z : P2_HOME_Z`. Programm 3 hat mit `P3_DONE`
  aber einen eigenen Zweig, daher ist der `P2_HOME_Z`-Fall hier nur fuer
  Programm 2 korrekt.

Beides habe ich bewusst **nicht** geaendert, weil es ausserhalb des Display-Themas
liegt. Sag Bescheid, ob ich das in einem separaten Commit mitnehmen soll.
