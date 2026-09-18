# Hairpin-Vereinzelung und -Zuführung

Prototypanlage, die Hairpins aus einem Bündel vereinzelt und sie positionsgenau
an den UR5e-Roboterarm der CleanLaser-Abisolierzelle übergibt.
Entstanden in der Bachelorarbeit von Jasper Mau am Lehrstuhl PEM, RWTH Aachen (2026).

| Kennwert | Wert |
| :-- | :-- |
| Zykluszeit | 43 s pro Hairpin |
| Wiederholgenauigkeit Übergabe | < 2 mm (Standardabweichung < 0,5 mm) |
| Vorrat ohne Nachladen | bis 80 Hairpins (getestet mit 25) |
| Fehlerfreie Zyklen im Dauerlauf | 93,3 % (mit passenden Sperrklinken) |

**3D-Daten (Fusion 360 / STL):** [Sciebo-Ordner](https://rwth-aachen.sciebo.de/s/onCe5WmpWoCQRFz?dir=/04_Thesis%20working%20content) (Passwort auf Anfrage bei Jasper Mau)
**Hintergrund, Auslegung und Messreihen:** Bachelorarbeit, Kapitel 5–7
**Menü im Detail:** [docs/menu_plan.md](docs/menu_plan.md)

---

## Aufbau

```
 Hairpin-Bündel
      │
 [1] Vibrationsschiene ── Schrittmotor Z (Neigung + Vibration)
      │
 [2] Vereinzelung ─────── 2 Sperrklinken, Servo 0 + 1 (MG996R)
      │
 [3] Rutsche ──────────── passiv
      │
 [4] Fixierung ────────── Seitenklemmen Servo 2 + 3, Hauptklemme Servo 4 (SG90)
      │
 [5] Transport ────────── Spindel, Schrittmotor Y
      │
 Übergabe an Roboterarm (zeitgesteuert)
```

Drei Mikrocontroller am I2C-Bus:

| Board | Adresse | Aufgabe | Code |
| :-- | :-- | :-- | :-- |
| ESP32 | Master | Ablauf, Display-Menü, serielle Konsole | `src_esp32/` |
| ELEGOO Uno + CNC-Shield | `0x33` | Schrittmotoren, Endschalter | `src_ELEGOOuno/` |
| Arduino Nano | `0x32` | Servos, Sensoreingänge | `src_Nano/` |

Versorgung: 230 V → Netzteil 12 V (Schrittmotoren) → DC/DC-Wandler 5 V (Servos, Logik).
Schaltplan: Bachelorarbeit, Abbildung B.4.

---

## 1. Bedienen

### Einschalten und Beladen
1. Netzteil einschalten. Das Display zeigt das Hauptmenü. In der Kopfzeile steht
   `BEREIT`. Steht dort `KEIN I2C`, antwortet ein Slave nicht (siehe Fehlersuche).
2. Das Hairpin-Bündel von der Stange auf die Vibrationsschiene schieben.
3. Prüfen, ob die **Sperrklinken zur Hairpin-Geometrie passen**. Passen sie nicht,
   vereinzelt die Anlage unzuverlässig (Doppelausgaben). Die Klinken sind mit
   zwei Schrauben befestigt.

### Tasten

| Taste | In Listen | Auf Wertzeilen | Während ein Programm läuft |
| :-- | :-- | :-- | :-- |
| ▲ / ▼ | Zeile wählen | Zeile wählen | – |
| ◀ | zurück | Wert verringern | – |
| ▶ | öffnen | Wert erhöhen | – |
| ENTER | auswählen / auslösen | – | **sofort anhalten** |
| ◀ 1,5 s halten | **NOT-HALT** | – | **NOT-HALT** |

Wird eine Pfeiltaste gehalten, wächst die Schrittweite: nach etwa 1 s ×10,
danach ×100.

### Programm starten
`Programme` → Programm wählen → `Durchläufe` einstellen → `START`

Im ersten Durchlauf fährt die Anlage die Achsen Z und Y auf ihre Endschalter
(Referenzfahrt). Danach wiederholt sie den Ablauf. Der Laufbildschirm zeigt den
aktuellen Schritt und die restlichen Durchläufe.

> **Roboter-Synchronisation:** Die Übergabe erfolgt nur nach Zeit. Die Anlage
> hält den Hairpin 4 s in der Übergabeposition und öffnet dann die Klemmen.
> Das Roboterprogramm muss auf diesen Takt abgestimmt sein.

### Handbetrieb
- `Schrittmotoren`: Referenzfahrt, Achsen einzeln verfahren, Treiber EIN/AUS
- `Servomotoren`: jeden Servo direkt stellen, `Grundstellung`

### Störung (z. B. verhakter Hairpin)
1. ENTER drücken, um anzuhalten (oder den Menüpunkt `NOT-HALT` wählen).
2. Den Hairpin von Hand entfernen.
3. Das Programm neu starten. Beim Neustart folgt wieder eine Referenzfahrt.

> ⚠️ **Der NOT-HALT arbeitet nur in der Software.** Er sendet einen Stopp-Befehl
> über I2C. Im Notfall die **Stromversorgung trennen**.

### Ruhezustand
- Nach 1 min ohne Eingabe kehrt das Display zum Hauptmenü zurück.
- Nach 10 min schaltet die Anlage die Schrittmotoren stromlos (Anzeige `MOT AUS`).
  Die Positionen sind danach nicht mehr gesichert. Vor dem nächsten Programmstart
  deshalb eine Referenzfahrt ausführen (passiert beim Start automatisch).

---

## 2. Anpassen

### Stufe A: am Display, ohne Programmieren
Ein Programm ist eine Liste von Blöcken. Blocktypen: Referenzfahrt, Fahren abs./rel.,
Vibration, Achse stoppen, Servo setzen, Warten.

**Neue Hairpin-Variante einrichten:**
1. `Programme` → `Neues Programm` (legt eine Kopie des Basisprogramms an)
2. `Umbenennen`, z. B. „HP 69mm“
3. `Ablauf + Werte` → Abschnitt *Fixiereinheit* → Servo 2 und 3 anpassen:

   | Hairpin-Länge | Servo 2 | Servo 3 |
   | :-- | :-- | :-- |
   | 62 mm | 580 | 350 |
   | 64 mm | 550 | 350 |
   | 69 mm | 430 | 380 |
   | 70 mm | 430 | 370 |

4. Die Anlage speichert automatisch nach 8 s. Solange etwas noch nicht gesichert
   ist, zeigt die Kopfzeile `* offen`.

Es gibt Platz für bis zu 8 Programme mit je 40 Blöcken. Die Programme liegen im
Flash des ESP32 und bleiben bei einem Neustart und bei neuer Firmware erhalten.
`Einstellungen` → `Werkseinstellungen` setzt alles zurück.

Blöcke lassen sich derzeit nur ändern, nicht einfügen oder löschen. Einen völlig
neuen Ablauf legt man daher in `program.cpp` an (siehe Stufe B).

### Stufe B: Code

**Werkzeuge:** VS Code mit der PlatformIO-Erweiterung. Jedes Board hat ein
eigenes Environment.

```bash
pio run -e esp32 -t upload
```
```bash
pio run -e uno -t upload
```
```bash
pio run -e nano -t upload
```
```bash
pio device monitor -e esp32
```

**Wo steht was?**

| Datei | Inhalt |
| :-- | :-- |
| `include/config_uno.h` | Soft-Limits und Geschwindigkeiten der Achsen, Referenzfahrt, Richtungsumkehr |
| `include/config_nano.h` | Servo-Pins, Min/Max je Servo, Richtungsumkehr |
| `include/config_display.h` | Display- und Tasten-Pins |
| `include/i2c_protocol.h` | Datenstrukturen zwischen den Boards. **Änderungen immer auf alle Boards flashen** |
| `src_esp32/program.cpp` | Basisprogramm (Werkseinstellung), Ablauf-Engine |
| `src_esp32/display_ui.cpp` | Menü |
| `src_esp32/main.cpp` | I2C-Funktionen (`axis_*`, `servo_set`), serielle Konsole |

**Belegung**

| Aktor | Anschluss | Funktion | Bereich |
| :-- | :-- | :-- | :-- |
| Schrittmotor Z | CNC-Shield Z, Endschalter D11 | Vibrationsschiene | 0 … 3000 Schritte |
| Schrittmotor Y | CNC-Shield Y, Endschalter D10 | Transport zur Übergabe | −7600 … 0 Schritte |
| Schrittmotor X | CNC-Shield X, Endschalter D9 | Reserve | 0 … 4000 Schritte |
| Servo 0 | Nano D7 | Sperrklinke (MG996R) | 100 … 560 |
| Servo 1 | Nano D8 | Sperrklinke (MG996R) | 100 … 540 |
| Servo 2 / 3 | Nano D9 / D10 | Seitenklemmen (SG90) | 0 … 800 |
| Servo 4 | Nano D11 | Hauptklemme (SG90) | 0 … 500 |
| Servo 5 | Nano D12 | Reserve | 100 … 900 |

Servowerte 0 … 1000 entsprechen 500 … 2500 µs. Befehle außerhalb der
Grenzen begrenzt der Slave oder verwirft sie.

**Neues Programm ab Werk:** In `program.cpp` eine Funktion nach dem Vorbild von
`defaultsBaseProgram()` anlegen. Wenn sich die Struktur `Program`/`Block` ändert,
`PROG_VERSION` erhöhen. Dann werden die gespeicherten Programme verworfen.

**Serielle Konsole** (115200 Baud, zuerst `t` eingeben, um sie zu aktivieren):

| Befehl | Wirkung |
| :-- | :-- |
| `scan` | I2C-Bus prüfen |
| `status` | Positionen und Servowerte ausgeben |
| `run5` / `runp2_5` | Programm 1 bzw. 2 mit 5 Durchläufen starten |
| `stop` | Lauf abbrechen |
| `STP_Y,ABS,-7500,2000` | Achse (X/Y/Z) fahren: `REL`, `ABS`, `TIMED`, `VIB`, `STOP`, `HOMING` |
| `SRV_2,430` | Servo stellen (Wert 0 … 1000) |
| `enAll` / `disAll` | Schrittmotortreiber ein/aus |

**Regeln für den Code:** kein `delay()` im Ablauf, denn die Zeitsteuerung läuft
über `millis()`. Kommentare auf Deutsch.

### Stufe C: Mechanik
Alle Druckteile sind aus PLA (FFF) und liegen im Sciebo-Ordner. Gelenke werden
einteilig mit Spalt gedruckt und durch mehrmaliges Bewegen freigebrochen.
Eine neue Hairpin-Geometrie braucht meist **neue Sperrklinken**:
1. Das Klinkenprofil an den Hairpin-Kopf anpassen.
2. Die Klinken drucken und mit zwei Schrauben tauschen.
3. Die Servo-0/1-Werte im Programm nachstellen.

---

## Fehlersuche

| Symptom | Ursache / Abhilfe |
| :-- | :-- |
| `KEIN I2C` in der Kopfzeile | Slave ohne Strom oder I2C-Kabel lose (SDA/SCL, gemeinsame Masse). `scan` in der Konsole ausführen |
| Display bleibt schwarz | Kabel zu lang oder lose. `TFT_SPI_HZ` in `config_display.h` senken |
| Taste reagiert nicht oder wird beim Start als gesperrt gemeldet | Verdrahtung prüfen. Die Taster-Schiene liegt auf **+3V3**, nicht auf GND |
| Achse fährt nicht | Treiber stromlos (`MOT AUS`) → Taste drücken. Oder das Ziel liegt außerhalb der Soft-Limits |
| Achse fährt in die falsche Richtung | `INVERT_*_DIR` in `config_uno.h` |
| Zwei Hairpins auf einmal | Sperrklinken passen nicht zur Geometrie, siehe Stufe C |
| Hairpin verhakt in der Rutsche | Hairpin-Beine entgraten. Übergänge zwischen den Segmenten prüfen |
| Servo reagiert nicht auf einen Wert | Der Wert liegt außerhalb seiner Grenze in `config_nano.h` |

---

## Offene Punkte
- Prozesssensoren (Reflexlichttaster an der Rutsche) sind montiert, aber noch nicht
  in den Ablauf eingebunden. Der Ablauf läuft bisher nur nach festen Zeiten.
- Es fehlt ein Rückmeldesignal vom Roboter. Heute synchronisiert man die Übergabe von Hand.
- Die Vereinzelung hängt von der Geometrie ab. Ein generatives Klinkendesign pro Variante ist denkbar.
- Die Rutsche könnte einen Deckel bekommen, dazu eine glattere Innenfläche und eine angepasste Neigung.
- Menü: ein Block-Editor zum Einfügen, Löschen und Verschieben sowie ein Status-Menü.
- Ein Hardware-Not-Halt zum Trennen der Motorversorgung.
