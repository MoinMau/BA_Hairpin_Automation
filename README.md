# BA_Hairpin_Automation
This Project is using a CleanLaser and a Universal Robot Arm to strip the isolation of the Ends of a Hairpin

## Vereinzelung

Die Hairpins haengen als Buendel auf einer Stange, Kopf an Kopf. Zwei getrennt
auf- und abfahrende Stangen tragen je einen Endeffektor:

* **Endeffektor 1 – Halter** haelt das Buendel auf, damit die Reihe nicht
  nachrutscht. Seine Stirnseite bildet die Kopf-Knickbiegung ab und laeuft
  senkrecht durch -- reine Anlageflaeche, keine Tasche. Sie ist breiter als
  die Biegung (`breite_faktor`), damit die Koepfe stabil abgestuetzt werden.
  Unter der Grundplatte laeuft nur eine schmale Stirnleiste weiter nach unten,
  nicht der ganze Koerper.
* **Endeffektor 2 – Schneider** faehrt zwischen zwei Koepfe und vereinzelt
  genau einen. Unter der Grundplatte sitzt ein einseitig angeschliffener Keil:
  Vorderflaeche senkrecht auf der Knickkontur, Rueckflaeche angestellt, unten
  eine schmale Schneidfase. Beim Herabfahren drueckt die Schraege den naechsten
  Pin weg.

Beide entstehen aus demselben Generator und teilen sich die Grundplatte mit
Wellenbohrung, Klemmschlitz und M3-Klemmung. Die gesamte Stirnform wird aus der
Knickbiegung des mitgegebenen Hairpins abgeleitet.

## Bauteile erzeugen

Im Browser:

```
python3 webapp/server.py
```

Waehlt den Hairpin aus, zeigt das erzeugte Bauteil in 3D und bietet
STEP, STL und Pruefprotokoll zum Download an -- siehe [webapp/README.md](webapp/README.md).

Auf der Kommandozeile:

```
python3 endeffektor.py --variante schneider --hairpin PEM-Referenzstator-Aussen.STEP
python3 endeffektor.py --variante halter    --hairpin PEM-Referenzstator-Aussen.STEP
python3 endeffektor.py --variante schneider --keilwinkel 25 --eintauchtiefe 4
python3 endeffektor.py --variante halter    --breite-faktor 3.0
```

Voraussetzung ist nur `pip install build123d`.

`sperrklinke.py` gehoert zu einem frueheren Ansatz und laeuft nur noch auf der
Kommandozeile; in der Weboberflaeche wird es nicht mehr angeboten.

