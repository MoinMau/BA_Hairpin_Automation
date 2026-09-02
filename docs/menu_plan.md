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
[Kopfzeile: Seitentitel | BEREIT / X.Z / LAEUFT / * offen / KEIN I2C]
│
├─ Programme                       (Liste, waechst mit angelegten Programmen)
│   ├─ Programm 1
│   │   ├─ Durchlaeufe      25     Vorgabe je Programm, 1-999
│   │   ├─ START
│   │   ├─ Ablauf                  → Blockliste, siehe unten
│   │   ├─ Kopieren                → neues Programm aus dieser Vorlage
│   │   ├─ Speichern
│   │   ├─ Loeschen                → mit Rueckfrage
│   │   └─ Zurueck
│   ├─ Programm 2 …
│   ├─ Neues Programm              → Kopie des ersten Programms
│   └─ Zurueck
│
├─ Schrittmotoren
│   ├─ Achse X / Y / Z             → Referenzfahrt, Schrittweite, Geschw.,
│   │                                 Fahren -/+, STOPP
│   ├─ Referenzfahrt alle
│   ├─ Treiber EIN / AUS
│   └─ Zurueck
│
├─ Servomotoren
│   ├─ Servo 0 D7 … Servo 5 D12    Stellwert, wirkt sofort
│   ├─ Grundstellung
│   └─ Zurueck
│
└─ NOT-HALT
```

### Ablauf und Bloecke

„Ablauf" listet die Bloecke des Programms mit ihren Werten:

```
Programm 3 Ablauf                     6/24
  1 Z Referenz *
  2 Y Referenz *
  3 Y abs -7500 *
  4 Servo 0 = 1000
 ...
 19 Y rel 7400
 20 Warten 4000ms
```

Ein `*` kennzeichnet Bloecke, die nur im ersten Durchlauf ausgefuehrt werden
(die Referenzfahrten in Programm 3). ENTER auf einem Block oeffnet dessen
Felder — welche das sind, haengt vom Blocktyp ab:

| Blocktyp | Felder |
| :-- | :-- |
| Referenzfahrt | Achse |
| Fahren absolut / relativ | Achse, Weg, Geschwindigkeit |
| Vibration | Achse, Amplitude, Frequenz |
| Achse stoppen | Achse |
| Servo setzen | Servo-Nummer, Stellwert |
| Warten | Zeit in ms |

Dazu bei jedem Block die Zeile „nur 1. Lauf".

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
(Autorepeat). Bewaffnet wird der Timer nur durch ein echtes Druck-Ereignis.

Umgekehrt gilt auf dem Laufbildschirm: **ENTER haelt sofort an**. Anhalten muss
leicht sein, Starten schwer.

**Beschleunigung beim Halten.** Wer einen Weg von 7400 Schritten in Zehnerschritten
einstellen muesste, waere lange beschaeftigt. Deshalb waechst die Schrittweite,
solange die Richtungstaste gehalten wird: erst der eingestellte Schritt, nach
etwa einer Sekunde das Zehnfache, danach das Hundertfache.

> Der Menuepunkt heisst „NOT-HALT", trennt aber nur softwareseitig ueber den
> I2C-Bus. Ein echter Nothalt muss die Motorversorgung hardwareseitig trennen
> (zwangsoeffnender Pilzkopf) und darf nicht davon abhaengen, dass Firmware,
> I2C-Bus und Uno noch funktionieren.

### Laufbildschirm

Sobald ein Programm laeuft, schaltet die Oberflaeche automatisch um:
Programmname, aktueller Block mit Kurzbeschreibung, verbleibende Durchlaeufe.
Navigation gesperrt. Das greift auch, wenn der Lauf ueber die serielle Konsole
(`run`, `runp2_5`) gestartet wurde.

---

## 3. Programme als Daten

Ein Programm ist eine **Liste von Bloecken** (`src_esp32/program.h`), keine
Zustandsmaschine im Quelltext mehr. Vorher liess sich ein neuer Ablauf nur
durch Programmieren und Flashen anlegen; jetzt kopiert man eine Vorlage am
Display und stellt die Werte ein. Genau das braucht man, wenn pro
Hairpin-Laenge ein eigenes Programm noetig ist.

**Blocktypen:** Referenzfahrt, Fahren absolut, Fahren relativ, Vibration,
Achse stoppen, Servo setzen, Warten. Jeder Block traegt zusaetzlich das Flag
„nur im ersten Durchlauf".

**Grenzen:** 8 Programme, je 40 Bloecke. Das sind rund 2,8 KB im RAM und
ebenso viel im NVS.

**Speicherung** im NVS des ESP32 (`Preferences`, Namensraum `hairpin`,
Schluessel `pg0`…`pg7`). Jedes Programm traegt `magic` und `version`; passt
eines nicht, wird es verworfen. Sind gar keine Programme gespeichert, werden
die Werkseinstellungen geladen. Beim Erweitern der Struktur muss
`PROG_VERSION` erhoeht werden.

**Schreibzeitpunkt:** 8 Sekunden nach der letzten Aenderung automatisch, dazu
die Zeile „Speichern" fuer sofortiges Sichern. Die Kopfzeile zeigt `* offen`,
solange etwas noch nicht im NVS steht.

**Werkseinstellungen** bilden die frueheren Zustandsmaschinen P1, P2 und P3
exakt nach. Eine Besonderheit bei Programm 1: dort liefen die beiden
Wartezeiten der Vereinzelung nicht nacheinander, sondern beide ab dem Start
der Vibration (1500 ms und 18000 ms ab demselben Zeitpunkt). Als
aufeinanderfolgende Bloecke sind das 1500 ms und danach 16500 ms — die
Gesamtdauer von 18000 ms bleibt gleich.

Die Greiferwerte in Programm 3 haengen von der Hairpin-Laenge ab (die alten
Quelltext-Kommentare stehen als Werkseinstellung in `program.cpp`):

| Hairpin | S2 | S3 |
| :-- | :-- | :-- |
| 62 mm | 580 | 350 |
| 64 mm | 550 | 350 |
| 69 mm | 430 | 380 |
| 70 mm | 430 | 370 |

Fuer jede Laenge ein eigenes Programm anzulegen ist damit: Programm 3 oeffnen,
„Kopieren", im Ablauf die beiden Servo-Bloecke anpassen.

---

## 4. Naechste Ausbaustufe


**Bloecke einfuegen, loeschen, verschieben.** Aktuell lassen sich die Werte
eines Blocks aendern, aber die Struktur eines Ablaufs nicht. Fuer neue
Programme aus einer Vorlage reicht das; um einen Ablauf voellig neu
aufzubauen, braeuchte es einen Block-Editor.

**Programmnamen aendern.** Kopien heissen automatisch „Programm N". Eine
Texteingabe ueber fuenf Tasten ist unhandlich; sinnvoller waere eine Liste
vorgegebener Namen, etwa nach Hairpin-Laenge.

**Status-Menue.** `print_status()` und `scanI2CBus()` sind vorhanden, haengen
aber am Serial-Monitor. Positionen, Servostellungen, Sensorwerte und I2C-Scan
am Display verfuegbar zu machen, waere wenig Aufwand.

---

## 5. Behobene Altlasten

Beim Umbau auf Bloecke mit korrigiert — alle drei waren fehlende `break;` bzw.
nicht erreichbare Zweige in der alten Zustandsmaschine:

- `P2_POSITION_Y_WAIT` fiel in `P2_SERVO_INIT` durch: die Servos liefen los,
  waehrend Y noch fuhr.
- `P2_Y_FORWARD_WAIT` fiel in `P2_Y_ROBOT_WAIT` durch, und zwar mit einem
  Zeitstempel aus einem frueheren Schritt. Die Roboter-Wartezeit in Programm 2
  wurde dadurch faktisch uebersprungen.
- `P1_DONE` sprang beim Wiederholen auf einen nicht erreichbaren
  `P2_HOME_Z`-Zweig.

Zusaetzlich prueft die Engine eine Fahrt erst 150 ms nach dem Absetzen des
Befehls auf „fertig". Vorher konnte ein Block sofort als abgeschlossen gelten,
wenn der Uno die Achse noch nicht als belegt gemeldet hatte.
