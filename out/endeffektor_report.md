# Pruefprotokoll endeffektor

## Kennwerte
- grundkoerper_volumen_mm3: 1848.41
- grundkoerper_solids: 1
- keil_quelle: Referenz (Platzhalter)
- keil_volumen_mm3: 374.17
- gesamt_volumen_mm3: 2222.56
- gesamt_solids: 1
- bauraum: [30.102, 18.0, 16.0]

## Verifikation der Referenzdatei

| Kriterium | Wert | Soll | Toleranz | Status |
|---|---|---|---|---|
| Versatz korrekt (X-Minimum bei 0) | 0.0 mm | 0.0 mm | 0.02 | OK |
| Bauraum X | 30.102 mm | 30.102 mm | 0.02 | OK |
| Bauraum Y | 18.0 mm | 18.0 mm | 0.02 | OK |
| Bauraum Z | 16.0 mm | 16.0 mm | 0.02 | OK |
| Volumen | 2222.563 mm3 | 2222.56 mm3 | 0.5 | OK |
| Plattenbreite Y | 18.0 mm | 18.0 mm | 0.02 | OK |
| Plattenoberkante Z | 16.0 mm | 16.0 mm | 0.02 | OK |
| Hauptbohrung gefunden (Anzahl Flaechen) | 1  | 1  | 0 | OK |
| Hauptbohrung Durchmesser | 6.2 mm | 6.2 mm | 0.02 | OK |
| Hauptbohrung Achse X | 16.692 mm | 16.692 mm | 0.02 | OK |
| Hauptbohrung Achse Y | -0.0 mm | 0.0 mm | 0.02 | OK |
| Klemmschraube gefunden (Anzahl Flaechen) | 2  | 2  | 0 | OK |
| Klemmschraube Durchmesser | 3.2 mm | 3.2 mm | 0.02 | OK |
| Klemmschraube Achse X | 23.792 mm | 23.792 mm | 0.02 | OK |
| Klemmschraube Achse Z | 13.0 mm | 13.0 mm | 0.02 | OK |
| Senkung gefunden (Anzahl Flaechen) | 2  | 2  | 0 | OK |
| Klemmschlitz: Waende bis zur Hinterkante | 2  | 2  | 0 | OK |
| Anzahl ebene Flaechen | 151  | 151  | 0 | OK |
| Anzahl B-Spline Flaechen | 75  | 75  | 0 | OK |
| Anzahl zylindrische Flaechen | 9  | 9  | 0 | OK |

## Pruefung des erzeugten Koerpers

| Kriterium | Wert | Soll | Toleranz | Status |
|---|---|---|---|---|
| Ergebnis ist EIN Solid | 1  | 1  | 0 | OK |
| Plattendicke Z | 6.0 mm | 6.0 mm | 0.02 | OK |
| Plattenbreite Y | 18.0 mm | 18.0 mm | 0.02 | OK |
| Volumen = Referenzvolumen | 2222.563 mm3 | 2222.56 mm3 | 0.5 | OK |