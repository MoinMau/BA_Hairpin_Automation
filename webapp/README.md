# Frontend

Browser-Oberflaeche fuer den Endeffektor-Generator `endeffektor.py`.

```
python3 webapp/server.py
```

Der Browser oeffnet sich von selbst auf <http://127.0.0.1:8765/>.
Mit `--port 8000` laeuft er woanders, mit `--no-browser` bleibt der Browser zu.

## Ablauf

1. **Bauteil** waehlen -- die beiden Endeffektoren der Vereinzelung:

   | | Aufgabe | Unterbau |
   |---|---|---|
   | **Endeffektor 1 – Halter** | haelt das Buendel auf | prismatischer Anlageblock, Kontur senkrecht durch |
   | **Endeffektor 2 – Schneider** | vereinzelt genau einen Hairpin | einseitig angeschliffener Keil |

2. **Hairpin-STEP** waehlen. Angeboten wird alles, was als `.step`/`.stp` im
   Projektordner liegt; weitere Dateien lassen sich per Drag & Drop ablegen.
   Der Hairpin ist fuer beide Bauteile Pflicht -- die ganze Stirnform wird aus
   der Knickbiegung seines Kopfes abgeleitet.
3. **Parameter** einstellen und *Bauteil generieren*.
4. **3D-Vorschau** pruefen: ziehen = drehen, Mausrad = zoomen,
   Shift-ziehen oder rechte Maustaste = verschieben. Ueber *Hairpin in 3D
   ansehen* laesst sich die Eingangsgeometrie kontrollieren.
5. **Herunterladen**: STEP (CAD), STL (Druck), Pruefprotokoll als MD/JSON,
   Schnitte als SVG. *PNG* sichert die aktuelle Ansicht fuer die Doku.

## Warum ohne Framework

Gebraucht wird nur die Python-Standardbibliothek plus `build123d`, das fuer die
Generatoren ohnehin installiert ist -- kein Flask, kein npm, kein CDN. Damit
laeuft die Oberflaeche offline und ueberlebt einen Rechnerwechsel ohne
Installationsrunde. Der 3D-Viewer (`static/viewer.js`) ist deshalb ein
eigener, kleiner WebGL-Renderer statt three.js.

## Aufbau

| Datei | Aufgabe |
|---|---|
| `server.py` | HTTP-Schnittstelle, ruft `lauf()` der Generatoren direkt auf |
| `static/index.html` | Seitengeruest |
| `static/app.js` | Ablaufsteuerung, baut das Formular aus `/api/schema` |
| `static/viewer.js` | STL einlesen und in WebGL darstellen |
| `static/style.css` | Gestaltung |

Das Parameterformular wird **nicht** von Hand gepflegt: `/api/schema` liest die
Parameter-dataclass der jeweiligen Variante aus (`ParameterHalter` bzw.
`ParameterSchneider`). Ein neues Feld in `endeffektor.py` erscheint dadurch von
allein im Browser. Beschriftung, Einheit und Gruppierung stehen in `FELD_INFO`
in `server.py` -- fehlt dort ein Eintrag, wird das Feld trotzdem angezeigt,
dann eben mit seinem Variablennamen.

Ergebnisse landen unter `webapp/jobs/<zeitstempel>-<bauteil>/`; die letzten 30
Laeufe bleiben liegen, aeltere werden aufgeraeumt. Hochgeladene Hairpins liegen
in `webapp/uploads/`, vernetzte Vorschauen in `webapp/cache/`. Alle drei Ordner
sind in `.gitignore`.

## Schnittstelle

| Route | Zweck |
|---|---|
| `GET /api/bauteile` | verfuegbare Generatoren |
| `GET /api/schema?bauteil=…` | Formularbeschreibung aus der dataclass |
| `GET /api/hairpins` | gefundene STEP-Dateien |
| `GET /api/hairpin-mesh?id=…` | Eingangsgeometrie als STL fuer die Vorschau |
| `POST /api/upload` | STEP hochladen (Body = Datei, Name im Header `X-Dateiname`) |
| `POST /api/generieren` | Lauf ausfuehren, liefert Kennwerte, Pruefung, Dateiliste |
| `GET /api/datei/<job>/<datei>` | Ergebnisdatei; `?download=1` erzwingt das Speichern |

Der Server bindet auf `127.0.0.1` und ist als lokales Werkzeug gedacht, nicht
fuer den Betrieb im Netz.
