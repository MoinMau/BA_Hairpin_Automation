"""
webapp/server.py -- Lokales Frontend fuer die parametrischen Generatoren.

Ablauf im Browser:
    1. Hairpin-STEP auswaehlen (oder per Drag & Drop hochladen)
    2. Parameter einstellen -> "Bauteil generieren"
    3. 3D-Vorschau drehen, Pruefprotokoll lesen
    4. STEP / STL / Report herunterladen

Bewusst OHNE Flask, FastAPI, npm oder CDN: nur die Python-Standardbibliothek
plus build123d, das fuer die Generatoren ohnehin gebraucht wird. Damit laeuft
die Oberflaeche auch offline und ohne zusaetzliche Installation.

Aufruf:
    python3 webapp/server.py
    python3 webapp/server.py --port 8000 --no-browser
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import io
import json
import mimetypes
import shutil
import sys
import threading
import time
import traceback
import webbrowser
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs, quote, unquote

HIER = Path(__file__).resolve().parent
PROJEKT = HIER.parent

# Die Generatoren liegen eine Ebene hoeher und werden als Modul importiert --
# nicht als Subprozess. So bekommen wir Kennwerte und Pruefliste als echte
# Python-Objekte zurueck statt sie aus der Konsolenausgabe zu fischen.
sys.path.insert(0, str(PROJEKT))

import endeffektor  # noqa: E402

STATIC = HIER / "static"
UPLOADS = HIER / "uploads"      # hochgeladene Hairpins
JOBS = HIER / "jobs"            # ein Ordner je Generierung
CACHE = HIER / "cache"          # tesselierte Hairpins fuer die Vorschau

STEP_ENDUNGEN = {".step", ".stp"}
MAX_UPLOAD = 64 * 1024 * 1024   # 64 MB
JOBS_BEHALTEN = 30

# OCCT ist nicht threadsicher; der Server ist es. Also darf immer nur ein
# Generierungslauf gleichzeitig durch.
CAD_LOCK = threading.Lock()


# ----------------------------------------------------------------------------
# 1. Bauteile -- was das Frontend anbieten darf
# ----------------------------------------------------------------------------
# Angeboten werden genau die beiden Endeffektoren der Vereinzelung. Beide
# entstehen aus demselben Generator und unterscheiden sich nur in der
# Parameterklasse -- und damit in dem, was unter der Grundplatte haengt.
def _start_endeffektor(p, outdir, stem, quelle):
    koerper, kw, checks = endeffektor.lauf(p, outdir, stem, hairpin=quelle)
    return koerper, kw, checks, stem


BAUTEILE = {
    "halter": {
        "titel": "Endeffektor 1 – Halter",
        "beschreibung": "Haelt das Buendel auf. Die Stirnseite bildet die "
                        "Kopf-Knickbiegung ab, laeuft senkrecht durch und ist "
                        "breiter als die Biegung, damit die Koepfe stabil "
                        "anliegen. Darunter bleibt nur eine schmale Stirnleiste.",
        "parameter": endeffektor.ParameterHalter,
        "hairpin": "pflicht",
        "starten": _start_endeffektor,
    },
    "schneider": {
        "titel": "Endeffektor 2 – Schneider",
        "beschreibung": "Vereinzelt genau einen Hairpin. Unter der Grundplatte "
                        "sitzt ein einseitig angeschliffener Keil, der zwischen "
                        "zwei Koepfe faehrt und den naechsten Pin wegdrueckt.",
        "parameter": endeffektor.ParameterSchneider,
        "hairpin": "pflicht",
        "starten": _start_endeffektor,
    },
}

# Beschriftungen fuer die Eingabefelder. Was hier fehlt, wird trotzdem
# angezeigt -- dann eben mit dem rohen Feldnamen. So taucht ein neuer
# Parameter in der dataclass automatisch im Formular auf.
#
# Beide Endeffektoren teilen sich die Grundplatte und die Konturerkennung;
# nur der Unterbau unterscheidet sie. Deshalb steht der gemeinsame Teil hier
# einmal und wird unten in beide Varianten hineinkopiert.
_GEMEINSAME_FELDER = {
    "laenge": ("Laenge (X): Anlagekante bis Hinterkante", "mm"),
    "breite": ("Breite (Y) – wird normal aus der Biegung abgeleitet", "mm"),
    "dicke": ("Dicke der Grundplatte (Z)", "mm"),
    "unterkante_z": ("Trennebene Grundplatte / Unterbau", "mm"),
    "bohrung_d": ("Bohrungsdurchmesser (Welle)", "mm"),
    "bohrung_x": ("Achslage der Wellenbohrung (X)", "mm"),
    "schlitz_breite": ("Klemmschlitzbreite (Y)", "mm"),
    "schraube_d": ("Schraubendurchmesser", "mm"),
    "schraube_x": ("Achslage der Klemmschraube (X)", "mm"),
    "senkung_d": ("Senkungsdurchmesser", "mm"),
    "senkung_tiefe": ("Senkungstiefe", "mm"),
    "mutter_sw": ("Schluesselweite der Mutter", "mm"),
    "mutter_spiel": ("Einlegespiel der Mutter", "mm"),
    "mutter_tiefe": ("Tiefe der Mutterntasche", "mm"),
    "mutter_drehung": ("Drehung der Mutterntasche", "Grad"),
    "eckradius": ("Radius der hinteren Ecken (0 = eckig)", "mm"),
    "kantenradius": ("Bruch der Oberkanten", "mm"),
    "min_wandstaerke": ("Minimale Wandstaerke", "mm"),
    "duesen_d": ("Duesendurchmesser des Druckers", "mm"),
    "kopf_spiel": ("Abstand Stirnflaeche <-> Hairpin", "mm"),
    "kopf_y": ("Kontur quer verschieben", "mm"),
    "kopf_breite_ableiten": ("Breite aus der Biegung ableiten", ""),
    "kopf_abtastung": ("Stuetzstellen der Anlagekante", "-"),
    "kopf_schnitt_dz": ("Hoehenschritt der Projektion", "mm"),
    "kopf_suchbreite": ("Suchbreite fuer die Biegung", "mm"),
    "kopf_kern_schwelle": ("Kruemmungsschwelle des Biegungskerns", "-"),
    "kopf_auslauf": ("Auslauf je Seite, in Kernbreiten", "-"),
    "breite_faktor": ("Stirnseite so viel breiter als die Biegung", "x"),
}

# Die Gruppen, die beide Varianten gemeinsam haben -- sie folgen dem
# variantenspezifischen Teil, damit oben im Formular das steht, was den
# jeweiligen Endeffektor ausmacht.
_GEMEINSAME_GRUPPEN = [
    ("Stirnform aus dem Hairpin",
     ["kopf_spiel", "kopf_y", "breite_faktor", "kopf_breite_ableiten",
      "kopf_abtastung"]),
    ("Grundplatte", ["laenge", "breite", "dicke", "unterkante_z"]),
    ("Hauptbohrung (Welle)", ["bohrung_d", "bohrung_x"]),
    ("Klemmung", ["schlitz_breite", "schraube_d", "schraube_x",
                  "senkung_d", "senkung_tiefe"]),
    ("Mutterntasche", ["mutter_sw", "mutter_spiel", "mutter_tiefe", "mutter_drehung"]),
    ("Kanten & Fertigung", ["eckradius", "kantenradius", "min_wandstaerke", "duesen_d"]),
    ("Konturerkennung (Feinabstimmung)",
     ["kopf_suchbreite", "kopf_kern_schwelle", "kopf_auslauf", "kopf_schnitt_dz"]),
]

FELD_INFO = {
    "halter": {
        **_GEMEINSAME_FELDER,
        "anlage_hoehe": ("Hoehe der Stirnleiste unter der Platte", "mm"),
        "anlage_tiefe": ("Tiefe der Stirnleiste (leer = automatisch)", "mm"),
        "hals_breite": ("Breite hinten am Hals (leer = so schmal wie moeglich)", "mm"),
        "hals_ab_x": ("Schulter ab X (leer = hinter der Stirnleiste)", "mm"),
        "hals_radius": ("Verrundung der Schulter (0 = harte Stufe)", "mm"),
        "_gruppen": [
            ("Stirnleiste – haelt das Buendel auf",
             ["anlage_hoehe", "anlage_tiefe"]),
            ("Taille – hinten schmaler fuer die Stange",
             ["hals_breite", "hals_ab_x", "hals_radius"]),
        ] + _GEMEINSAME_GRUPPEN,
    },
    "schneider": {
        **_GEMEINSAME_FELDER,
        "klinge_hoehe": ("Gebaute Hoehe des Keils", "mm"),
        "eintauchtiefe": ("Arbeitshub – so tief taucht der Keil ein", "mm"),
        "keilwinkel": ("Anstellung der Keilrueckflaeche", "Grad"),
        "schneide_fase": ("Dicke der Schneidkante (Druckgrenze!)", "mm"),
        "klinge_breite": ("Keilbreite (leer = so breit wie die Platte)", "mm"),
        "platte_ruecksprung": ("Platte hinter der Keilvorderkante", "mm"),
        "platte_freiwinkel": ("Ruecknahme der Platte nach oben (0 = senkrecht)", "Grad"),
        "_gruppen": [
            ("Keil – vereinzelt den Hairpin",
             ["klinge_hoehe", "eintauchtiefe", "keilwinkel", "schneide_fase",
              "klinge_breite"]),
            ("Freistellung der Grundplatte",
             ["platte_ruecksprung", "platte_freiwinkel"]),
        ] + _GEMEINSAME_GRUPPEN,
    },
}


def schema(bauteil: str) -> dict:
    """Formularbeschreibung direkt aus der dataclass ableiten.

    Damit bleiben Formular und Generator zwangslaeufig synchron: kommt in
    Parameter ein Feld dazu, erscheint es im Browser, ohne dass hier etwas
    nachgezogen werden muss.
    """
    eintrag = BAUTEILE[bauteil]
    info = FELD_INFO.get(bauteil, {})
    felder = {}
    for f in dataclasses.fields(eintrag["parameter"]):
        # Wegen "from __future__ import annotations" sind die Typen Strings.
        typ = str(f.type).replace(" ", "")
        beschriftung, einheit, *rest = info.get(f.name, (f.name, ""))
        auswahl = rest[0] if rest else None
        standard = f.default
        # Ganzzahlen muessen als solche durchgereicht werden: kopf_abtastung
        # landet sonst als 25.0 in range() und bricht den Lauf ab.
        if typ == "bool":
            # Als Auswahlfeld: das Frontend kennt nur Text-, Zahlen- und
            # Auswahlfelder, und "ja"/"nein" liest sich besser als eine 0/1.
            feldtyp, auswahl = "ja_nein", ["ja", "nein"]
            standard = "ja" if standard else "nein"
        elif "None" in typ or typ == "str":
            feldtyp = "text"
        elif typ == "int":
            feldtyp = "ganzzahl"
        else:
            feldtyp = "zahl"
        felder[f.name] = {
            "name": f.name,
            "label": beschriftung,
            "einheit": einheit,
            "typ": feldtyp,
            "optional": "None" in typ,
            "auswahl": auswahl,
            "standard": standard,
        }

    gruppen = []
    vergeben = set()
    for name, feldnamen in info.get("_gruppen", []):
        drin = [felder[n] for n in feldnamen if n in felder]
        vergeben.update(n for n in feldnamen if n in felder)
        if drin:
            gruppen.append({"titel": name, "felder": drin})
    rest_felder = [v for k, v in felder.items() if k not in vergeben]
    if rest_felder:
        gruppen.append({"titel": "Weitere Parameter", "felder": rest_felder})

    return {
        "bauteil": bauteil,
        "titel": eintrag["titel"],
        "beschreibung": eintrag["beschreibung"],
        "hairpin": eintrag["hairpin"],          # "pflicht" | "optional" | "nein"
        "gruppen": gruppen,
    }


def parameter_bauen(bauteil: str, werte: dict):
    """Rohe JSON-Werte in eine getypte Parameter-dataclass umsetzen.

    Unbekannte Schluessel werden ignoriert, leere Felder fallen auf den
    Standardwert zurueck. Ungueltige Zahlen melden sich mit Feldnamen.
    """
    p = BAUTEILE[bauteil]["parameter"]()
    beschreibung = {f["name"]: f for g in schema(bauteil)["gruppen"] for f in g["felder"]}

    for name, roh in (werte or {}).items():
        feld = beschreibung.get(name)
        if feld is None:
            continue
        if roh is None or (isinstance(roh, str) and roh.strip() == ""):
            if feld["optional"]:
                setattr(p, name, None)
            continue
        if feld["typ"] == "ja_nein":
            # Muss VOR der Auswahlpruefung stehen: sonst landet der String
            # "ja" in der dataclass, wo ein bool erwartet wird.
            if str(roh) not in ("ja", "nein"):
                raise ValueError(f"{feld['label']}: '{roh}' ist weder ja noch nein.")
            setattr(p, name, str(roh) == "ja")
        elif feld["auswahl"]:
            if roh not in feld["auswahl"]:
                raise ValueError(f"{feld['label']}: '{roh}' ist keine gueltige Auswahl.")
            setattr(p, name, roh)
        elif feld["typ"] == "ganzzahl":
            try:
                setattr(p, name, int(float(str(roh).replace(",", "."))))
            except ValueError:
                raise ValueError(f"{feld['label']}: '{roh}' ist keine ganze Zahl.")
        elif feld["typ"] == "zahl" or feld["optional"]:
            try:
                setattr(p, name, float(str(roh).replace(",", ".")))
            except ValueError:
                raise ValueError(f"{feld['label']}: '{roh}' ist keine Zahl.")
        else:
            setattr(p, name, str(roh))
    return p


# ----------------------------------------------------------------------------
# 2. Hairpin-Dateien
# ----------------------------------------------------------------------------
def hairpins() -> list[dict]:
    """Alle verfuegbaren STEP-Dateien: mitgelieferte und hochgeladene."""
    gefunden = []
    for ordner, quelle in ((PROJEKT, "Projekt"), (UPLOADS, "Hochgeladen")):
        if not ordner.is_dir():
            continue
        for pfad in sorted(ordner.iterdir()):
            if pfad.is_file() and pfad.suffix.lower() in STEP_ENDUNGEN:
                gefunden.append({
                    "id": f"{quelle}/{pfad.name}",
                    "name": pfad.name,
                    "quelle": quelle,
                    "groesse_kb": round(pfad.stat().st_size / 1024, 1),
                    "geaendert": datetime.fromtimestamp(pfad.stat().st_mtime).strftime("%d.%m.%Y %H:%M"),
                })
    return gefunden


def hairpin_pfad(kennung: str) -> Path:
    """Kennung aus dem Frontend auf einen Pfad abbilden -- ohne Ausbruch.

    Es wird nur akzeptiert, was auch in hairpins() steht. Damit kann ueber die
    Schnittstelle keine beliebige Datei vom Rechner gelesen werden.
    """
    for h in hairpins():
        if h["id"] == kennung:
            basis = PROJEKT if h["quelle"] == "Projekt" else UPLOADS
            return basis / h["name"]
    raise ValueError(f"Unbekannte Hairpin-Datei: {kennung}")


def hairpin_mesh(kennung: str) -> Path:
    """Hairpin fuer die Vorschau tessellieren; Ergebnis wird zwischengespeichert.

    Der Cache-Schluessel enthaelt die Aenderungszeit -- eine ueberschriebene
    Datei wird also neu vernetzt.
    """
    quelle = hairpin_pfad(kennung)
    CACHE.mkdir(parents=True, exist_ok=True)
    ziel = CACHE / f"{quelle.stem}_{int(quelle.stat().st_mtime)}.stl"
    if ziel.exists():
        return ziel
    with CAD_LOCK:
        if not ziel.exists():                       # zweiter Blick unter Sperre
            form = endeffektor.import_step(str(quelle))
            endeffektor.export_stl(form, str(ziel), tolerance=0.05, angular_tolerance=0.3)
    return ziel


# ----------------------------------------------------------------------------
# 3. Generierung
# ----------------------------------------------------------------------------
def jobs_aufraeumen():
    """Nur die juengsten Laeufe behalten -- der Ordner soll nicht zuwachsen."""
    if not JOBS.is_dir():
        return
    alle = sorted((d for d in JOBS.iterdir() if d.is_dir()),
                  key=lambda d: d.stat().st_mtime, reverse=True)
    for alt in alle[JOBS_BEHALTEN:]:
        shutil.rmtree(alt, ignore_errors=True)


def generieren(bauteil: str, kennung: str | None, werte: dict) -> dict:
    """Einen Generatorlauf ausfuehren und alles zurueckgeben, was die UI braucht."""
    if bauteil not in BAUTEILE:
        raise ValueError(f"Unbekanntes Bauteil: {bauteil}")
    eintrag = BAUTEILE[bauteil]
    p = parameter_bauen(bauteil, werte)

    job_id = f"{time.strftime('%Y%m%d-%H%M%S')}-{bauteil}"
    outdir = JOBS / job_id
    outdir.mkdir(parents=True, exist_ok=True)

    # Die Generatoren schreiben ihren Fortschritt nach stdout. Den fangen wir
    # ab und reichen ihn als Protokoll an die Oberflaeche weiter.
    modus = eintrag["hairpin"]
    if modus == "nein":
        kennung = None
    if modus == "pflicht" and not kennung:
        raise ValueError("Fuer dieses Bauteil muss ein Hairpin ausgewaehlt werden.")
    quelle = hairpin_pfad(kennung) if kennung else None

    log = io.StringIO()
    with CAD_LOCK, contextlib.redirect_stdout(log):
        koerper, kw, checks, stem = eintrag["starten"](
            p, outdir, f"endeffektor_{bauteil}", quelle)

    bb = koerper.bounding_box()
    dateien = sorted(f.name for f in outdir.iterdir() if f.is_file())

    jobs_aufraeumen()
    return {
        "job": job_id,
        "bauteil": bauteil,
        "titel": eintrag["titel"],
        "stem": stem,
        "hairpin": kennung,
        "parameter": dataclasses.asdict(p),
        "kennwerte": kw,
        "pruefung": checks,
        "alles_ok": all(c["status"] == "OK" for c in checks),
        "bauraum": [round(bb.size.X, 3), round(bb.size.Y, 3), round(bb.size.Z, 3)],
        "volumen_mm3": round(koerper.volume, 2),
        "dateien": dateien,
        # quote(), weil Dateinamen aus dem Hairpin-Namen entstehen und
        # damit Leerzeichen oder Umlaute enthalten koennen.
        "stl": f"/api/datei/{quote(job_id)}/{quote(stem + '.stl')}",
        "log": log.getvalue(),
    }


# ----------------------------------------------------------------------------
# 4. HTTP
# ----------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    server_version = "HairpinAutomation/1.0"
    protocol_version = "HTTP/1.1"

    # --- kleine Helfer ---------------------------------------------------
    def _senden(self, status: int, koerper: bytes, typ: str, extra: dict | None = None):
        self.send_response(status)
        self.send_header("Content-Type", typ)
        self.send_header("Content-Length", str(len(koerper)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(koerper)

    def _json(self, daten, status: int = 200):
        self._senden(status, json.dumps(daten, ensure_ascii=False).encode("utf-8"),
                     "application/json; charset=utf-8")

    def _fehler(self, status: int, text: str):
        self._json({"fehler": text}, status)

    def _datei(self, pfad: Path, download_als: str | None = None):
        if not pfad.is_file():
            return self._fehler(404, f"Datei nicht gefunden: {pfad.name}")
        typ = mimetypes.guess_type(pfad.name)[0] or "application/octet-stream"
        if pfad.suffix.lower() in {".step", ".stp", ".stl"}:
            typ = "application/octet-stream"
        extra = {}
        if download_als:
            extra["Content-Disposition"] = f'attachment; filename="{download_als}"'
        self._senden(200, pfad.read_bytes(), typ, extra)

    def _body(self) -> bytes:
        laenge = int(self.headers.get("Content-Length") or 0)
        if laenge > MAX_UPLOAD:
            raise ValueError("Datei zu gross (max. 64 MB).")
        return self.rfile.read(laenge) if laenge else b""

    # --- Routen ----------------------------------------------------------
    def do_GET(self):
        weg = urlparse(self.path)
        pfad = unquote(weg.path)
        try:
            if pfad == "/" or pfad == "/index.html":
                return self._datei(STATIC / "index.html")

            if pfad.startswith("/static/"):
                name = pfad[len("/static/"):]
                ziel = (STATIC / name).resolve()
                if STATIC.resolve() not in ziel.parents:
                    return self._fehler(403, "Zugriff verweigert.")
                return self._datei(ziel)

            if pfad == "/api/bauteile":
                return self._json([{"id": k, "titel": v["titel"],
                                    "beschreibung": v["beschreibung"],
                                    "hairpin": v["hairpin"]}
                                   for k, v in BAUTEILE.items()])

            if pfad == "/api/schema":
                bauteil = parse_qs(weg.query).get("bauteil", ["sperrklinke"])[0]
                if bauteil not in BAUTEILE:
                    return self._fehler(404, f"Unbekanntes Bauteil: {bauteil}")
                return self._json(schema(bauteil))

            if pfad == "/api/hairpins":
                return self._json(hairpins())

            if pfad == "/api/hairpin-mesh":
                kennung = parse_qs(weg.query).get("id", [""])[0]
                return self._datei(hairpin_mesh(kennung))

            if pfad.startswith("/api/datei/"):
                teile = pfad[len("/api/datei/"):].split("/")
                if len(teile) != 2:
                    return self._fehler(400, "Pfad erwartet: /api/datei/<job>/<datei>")
                job, name = teile
                # Beide Teile muessen einfache Namen sein. Ein Vergleich der
                # aufgeloesten Pfade reicht hier NICHT: "/api/datei/../server.py"
                # loest sauber auf und wuerde durchrutschen.
                if any(t in ("", ".", "..") or t != Path(t).name for t in (job, name)):
                    return self._fehler(403, "Zugriff verweigert.")
                ziel = (JOBS / job / name).resolve()
                if ziel.parent.parent != JOBS.resolve():
                    return self._fehler(403, "Zugriff verweigert.")
                als = name if "download" in parse_qs(weg.query) else None
                return self._datei(ziel, als)

            return self._fehler(404, "Unbekannter Pfad.")
        except ValueError as e:
            self._fehler(400, str(e))
        except Exception as e:                       # noqa: BLE001 -- UI soll etwas sehen
            traceback.print_exc()
            self._fehler(500, f"{type(e).__name__}: {e}")

    def do_POST(self):
        weg = urlparse(self.path)
        pfad = unquote(weg.path)
        try:
            if pfad == "/api/upload":
                # Die Datei kommt roh im Body, der Name im Header -- damit
                # brauchen wir kein multipart/form-data zu zerlegen.
                name = Path(unquote(self.headers.get("X-Dateiname", ""))).name
                if not name or Path(name).suffix.lower() not in STEP_ENDUNGEN:
                    return self._fehler(400, "Nur .step- oder .stp-Dateien werden angenommen.")
                daten = self._body()
                if not daten:
                    return self._fehler(400, "Leere Datei.")
                UPLOADS.mkdir(parents=True, exist_ok=True)
                (UPLOADS / name).write_bytes(daten)
                return self._json({"id": f"Hochgeladen/{name}", "name": name,
                                   "hairpins": hairpins()})

            if pfad == "/api/generieren":
                anfrage = json.loads(self._body() or b"{}")
                ergebnis = generieren(anfrage.get("bauteil", "sperrklinke"),
                                      anfrage.get("hairpin"),
                                      anfrage.get("parameter", {}))
                return self._json(ergebnis)

            return self._fehler(404, "Unbekannter Pfad.")
        except ValueError as e:
            self._fehler(400, str(e))
        except Exception as e:                       # noqa: BLE001
            traceback.print_exc()
            self._fehler(500, f"{type(e).__name__}: {e}")

    do_HEAD = do_GET

    def log_message(self, format, *args):            # noqa: A002
        # Nur Fehler melden; die normalen 200er wuerden das Generator-Protokoll
        # in der Konsole zumuellen.
        if not str(args[1] if len(args) > 1 else "").startswith("2"):
            sys.stderr.write("  %s\n" % (format % args))


def main():
    ap = argparse.ArgumentParser(description="Frontend fuer die Hairpin-Generatoren")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--no-browser", action="store_true", help="Browser nicht automatisch oeffnen")
    a = ap.parse_args()

    for ordner in (UPLOADS, JOBS, CACHE):
        ordner.mkdir(parents=True, exist_ok=True)

    adresse = f"http://{a.host}:{a.port}/"
    server = ThreadingHTTPServer((a.host, a.port), Handler)
    print("Hairpin-Automation -- Frontend")
    print(f"  Projekt : {PROJEKT}")
    print(f"  Adresse : {adresse}")
    print("  Beenden : Strg+C\n")
    if not a.no_browser:
        threading.Timer(0.7, lambda: webbrowser.open(adresse)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nBeendet.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
