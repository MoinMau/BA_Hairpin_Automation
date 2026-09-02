# TFT-Menuefuehrung fuer die Hairpin-Vereinzelung

Stand: 02.09.2026 · Branch `Otto_Display_Menu`

---

## 1. Hardware

### 1.1 Display — 1.8" TFT SPI, ST7735, 128x160, V1.2

| Modul-Pin | Signal | ESP32-WROOM-32 |
| :-- | :-- | :-- |
| 1 | LED / BL | 3V3 ueber 47 Ω |
| 2 | SCK | GPIO18 |
| 3 | SDA (MOSI) | GPIO23 |
| 4 | A0 / DC | GPIO4 |
| 5 | RESET | GPIO16 |
| 6 | CS | GPIO5 |
| 7 | GND | GND |
| 8 | VCC | +3V3 |

Das Display haengt am SPI, die Slaves am I2C (GPIO21/22) — zwei getrennte Busse,
die Menue-Ausgabe kann die Motorsteuerung also nicht ausbremsen.

Panel-Variante: `INITR_BLACKTAB`, Querformat um 180° gedreht (`TFT_ROTATION 3`).

> **ESP32-WROVER:** Dort sind GPIO16/17 fest fuer das PSRAM verdrahtet und als
> GPIO unbrauchbar. Nur auf WROOM-32 sind sie frei.

### 1.2 Bedientasten

| Taste | GPIO |
| :-- | :-- |
| UP | 27 |
| DOWN | 32 |
| LEFT | 33 |
| RIGHT | 26 |
| ENTER | 25 |

Die gemeinsame Schiene der Taster liegt auf **+3V3**, nicht auf GND.

Grund: Die verbauten Taster haben einen festen Widerstand von rund 5 kΩ
zwischen Signal und GND, parallel zum Kontakt. Gegen den internen Pullup
(rund 45 kΩ) ergibt das im Ruhezustand nur 0,33 V — der Eingang laege dauerhaft
auf LOW und ein Tastendruck waere nicht erkennbar. Mit der Schiene auf +3V3
wirkt derselbe Widerstand als Pulldown:

| Zustand | Pfad | Pegel |
| :-- | :-- | :-- |
| Ruhe | Pin ueber 5 kΩ nach GND | LOW |
| Gedrueckt | Pin direkt auf +3V3 | HIGH |

Zusaetzlich ist der interne Pulldown aktiv: bricht ein Draht, liest der Eingang
weiterhin LOW statt zu floaten. Keiner der fuenf GPIOs ist ein Strapping-Pin,
ein HIGH beim Booten ist also unkritisch. Umschaltbar ueber `BTN_ACTIVE_HIGH`
in `config_display.h`.

Bei vierbeinigen Mikrotastern sind jeweils zwei Beine intern dauerhaft
verbunden — die Kontakte muessen ueber die **Diagonale** abgegriffen werden.

Entprellung 30 ms, Autorepeat nach 450 ms mit 120 ms Takt. ENTER hat bewusst
kein Autorepeat, damit ein langer Druck keine Aktion doppelt ausloest.

Tasten, die beim Start bereits auf LOW liegen, werden gesperrt und auf dem
Startbild sowie auf Serial gemeldet. Das ist praktisch immer ein
Verdrahtungsfehler; ohne die Sperre wuerde eine einzige klemmende Taste die
gesamte Bedienung blockieren. Die Sperre faellt, sobald die Taste einmal
losgelassen wurde.

### 1.3 Software-Stack

`Adafruit GFX` + `Adafruit ST7735` statt `TFT_eSPI`: die gesamte Konfiguration
liegt damit in `include/config_display.h` und im Repository, statt in
gepatchten Library-Dateien.

**Wichtig für die Wartung:** `Adafruit_ST7735::initR()` ruft intern `begin(0)`
auf und setzt dort `SPI_DEFAULT_FREQ = 32 MHz` (`Adafruit_ST77xx.cpp:35`). Die
gesamte Init-Sequenz laeuft also mit 32 MHz, unabhaengig von `setSPISpeed()` —
das wirkt erst auf die nachfolgenden Zeichenbefehle. Bei langen Leitungen kommen
die Init-Befehle verstuemmelt an und das Panel bleibt schwarz. Da `begin()`
nicht virtuell ist, laesst sich das nicht ueberschreiben. `tftInitPanel()` in
`display_ui.cpp` faengt es ab: nach `initR()` wird auf `TFT_SPI_HZ`
heruntergeschaltet und die Einschaltbefehle (`SWRESET`, `SLPOUT`, `COLMOD`,
`NORON`, `DISPON`) werden bei diesem sicheren Takt noch einmal gesendet.

Aktuell laeuft das Panel mit Software-SPI (`TFT_USE_SOFT_SPI 1`). Auf
Hardware-SPI umzustellen macht den Bildaufbau fluessiger und ist mit dem
Init-Fix wahrscheinlich problemlos moeglich.

---

## 2. Menuestruktur

```
[Kopfzeile: Seitentitel | Live-Status: BEREIT / X.Z / P2 RUN / Istposition / KEIN I2C]
│
├─ Programme
│   ├─ Programm 1  Hairpin ─┐
│   ├─ Programm 2           ├→  Durchlaeufe [1-99]  →  START
│   ├─ Programm 3           ┘
│   └─ Zurueck
│
├─ Schrittmotoren
│   ├─ Achse X ─┐
│   ├─ Achse Y ─┼→  Referenzfahrt
│   ├─ Achse Z ─┘   Schrittweite   1 / 10 / 100 / 1000
│   │                Geschw. St/s   50-3000
│   │                Fahren  - / +
│   │                STOPP
│   ├─ Referenzfahrt alle
│   ├─ Treiber EIN / Treiber AUS
│   └─ Zurueck
│
├─ Servomotoren
│   ├─ Servo 0 D7 … Servo 5 D12   Stellwert 0-1000, wirkt sofort
│   ├─ Grundstellung              (Werte aus dem Start von Programm 1)
│   └─ Zurueck
│
└─ NOT-HALT
```

### Bedienlogik

| Taste | Liste | Wertzeile | Fahrzeile | Laufender Betrieb |
| :-- | :-- | :-- | :-- | :-- |
| UP / DOWN | Zeile wechseln | Zeile wechseln | Zeile wechseln | — |
| LEFT | eine Ebene zurueck | Wert − | Achse − | — |
| RIGHT | Unterseite oeffnen | Wert + | Achse + | — |
| ENTER | oeffnen / ausloesen | — | — | **NOT-HALT** |
| LEFT 1,5 s halten | **NOT-HALT** | (gesperrt) | (gesperrt) | **NOT-HALT** |

Der NOT-HALT ist zusaetzlich zum Menuepunkt als globale Halte-Geste erreichbar,
damit man ihn nicht aus einem Untermenue heraussuchen muss. Auf Wert- und
Fahrzeilen ist die Geste gesperrt, weil man LEFT dort absichtlich haelt
(Autorepeat) — sonst wuerde das Verkleinern eines Werts einen Abbruch ausloesen.
Bewaffnet wird der Timer nur durch ein echtes Druck-Ereignis, nicht durch den
blossen Pegel.

Umgekehrt gilt auf dem Laufbildschirm: **ENTER haelt sofort an**. Anhalten muss
leicht sein, Starten schwer — deshalb braucht der Start den Umweg ueber
Programmwahl und Durchlaufzahl.

> Der Menuepunkt heisst „NOT-HALT", trennt aber nur softwareseitig ueber den
> I2C-Bus. Ein echter Nothalt muss die Motorversorgung hardwareseitig trennen
> (zwangsoeffnender Pilzkopf) und darf nicht davon abhaengen, dass Firmware,
> I2C-Bus und Uno noch funktionieren.

### Laufbildschirm

Sobald `sequence_isRunning()` true wird, schaltet die Oberflaeche automatisch
um: Programmnummer, verbleibende Durchlaeufe, Navigation gesperrt. Das greift
auch, wenn der Lauf ueber die serielle Konsole (`run`, `runp2_5`) gestartet
wurde — Menue und Konsole zeigen damit immer denselben Zustand. Endet die
Sequenz, kehrt die Anzeige selbsttaetig ins Menue zurueck.

---

## 3. Aufbau des Codes

| Datei | Inhalt |
| :-- | :-- |
| `include/config_display.h` | Pins, Panel-Variante, SPI-Takt, Tastenparameter |
| `src_esp32/buttons.*` | Entprellung, Autorepeat, Sperre klemmender Tasten |
| `src_esp32/machine_api.h` | Deklariert die in `main.cpp` liegenden Maschinenfunktionen |
| `src_esp32/display_ui.*` | Menuemodell, Renderer, Navigation |
| `src_esp32/main.cpp` | unveraendert bis auf `ui_begin()`/`ui_update()` und fuenf Zugriffsfunktionen |

Das Menue steckt vollstaendig in Tabellen (`MenuPage` / `MenuRow`). Ein Renderer
und ein Navigationsstack bedienen alle Seiten — ein neuer Menuepunkt ist eine
Tabellenzeile, keine neue Zeichenfunktion. Zeilentypen: `ROW_SUBMENU`,
`ROW_ACTION`, `ROW_VALUE`, `ROW_CHOICE`, `ROW_JOG`, `ROW_BACK`.

Seiten mit mehr als sieben Zeilen scrollen; die Position steht als `3/8` rechts
in der Fusszeile, damit nichts die Werte ueberdeckt.

`machine_api.h` verschiebt keinen Code, sondern deklariert nur die vorhandenen
Funktionen. Menue und serielle Konsole rufen damit dieselbe API auf, ohne dass
die erprobte Ablaufsteuerung angefasst werden musste. Ergaenzt wurden in
`main.cpp` lediglich `sequence_isRunning()`, `sequence_program()`,
`sequence_remainingRuns()`, `sequence_abort()`, `servo_readAll()` und
`i2c_devicePresent()`.

**Buslast:** Der Maschinenstatus wird alle 300 ms geholt und zwischengespeichert
(rund 3 zusaetzliche I2C-Zugriffe pro Sekunde). Alle 2 s wird geprueft, ob der
Uno ueberhaupt antwortet; fehlt er, entfaellt der Statuspoll und die Kopfzeile
zeigt `KEIN I2C`. Das Menue laesst sich damit auch ohne angeschlossene Slaves
bedienen. Neu gezeichnet wird nur, was sich geaendert hat.

---

## 4. Naechste Ausbaustufe

**Parameter-Menue.** In `updateSequence()` stehen die Werte fest im Code:
`axis_rel(AXIS_Y, 2000, 800)`, `axis_vibrate(AXIS_Z, 1, 50)`, die 18000 ms
Vereinzelungsdauer, `axis_abs(AXIS_Y, -7500, 2000)`. Jede Aenderung erfordert
neu zu flashen. Ein Menuepunkt „Parameter" plus Speicherung im NVS des ESP32
(`Preferences`) wuerde Parameterstudien direkt an der Anlage ermoeglichen — das
ist der eigentliche Nutzen des Displays.

**Status-Menue.** `print_status()` und `scanI2CBus()` sind vorhanden, haengen
aber am Serial-Monitor. Positionen, Servostellungen, Sensorwerte und I2C-Scan
am Display verfuegbar zu machen, waere wenig Aufwand.

**Klartext-Schrittanzeige.** `updateSequence()` koennte den aktuellen Schritt in
eine Variable schreiben, die der Laufbildschirm anzeigt.

---

## 5. Randnotizen zum bestehenden Code

Beim Einlesen aufgefallen, bewusst nicht geaendert, weil ausserhalb des
Display-Themas:

- `src_esp32/main.cpp` — `case P2_POSITION_Y_WAIT:` hat kein `break;` und faellt
  in `P2_SERVO_INIT` durch. Die Servos werden dadurch schon gesetzt, waehrend Y
  noch faehrt.
- `src_esp32/main.cpp` — beim Wiederholen springt `P1_DONE` auf
  `(currentProgram == 1) ? P1_HOME_Z : P2_HOME_Z`. Programm 3 hat mit `P3_DONE`
  aber einen eigenen Zweig, der Fall greift dort also nicht.

Beides sollte vor der Umstellung auf ein Parameter-Menue angefasst werden.
