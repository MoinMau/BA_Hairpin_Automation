"""
sperrklinke.py -- Parametrischer Generator fuer Sperrklinken.

Liest eine Hairpin-Geometrie (STEP) ein, leitet daraus die Wirkkontur ab und
erzeugt eine angepasste Sperrklinke als STEP / STL / SVG inkl. Pruefprotokoll.

Benoetigt nur:  pip install build123d
Kein externes CAD-Programm.

Aufruf:
    python sperrklinke.py
        -> verwendet STEP_DATEI (siehe unten) und die Vorgaben in Parameter

    python sperrklinke.py anderer_pin.step --z 20 --spiel 0.25 --leg rechts
        -> uebersteuert beides fuer einen einzelnen Lauf
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass, asdict
from pathlib import Path

HIER = Path(__file__).resolve().parent

# ============================================================================
# >>> HIER ANPASSEN <<<  Pfad zur STEP-Datei des Hairpins
#
# Entweder nur der Dateiname -- dann wird die Datei neben diesem Skript gesucht:
#     STEP_DATEI = HIER / "PEM-Referenzstator-Aussen.STEP"
# oder ein vollstaendiger Pfad, z.B.:
#     STEP_DATEI = Path("/Users/moinmau/Desktop/pin_v3.step")
#     STEP_DATEI = Path(r"C:\Messungen\pin_v3.step")        # Windows
#
# Alle uebrigen Groessen (Kontakthoehe, Spiel, Eingriffstiefe, Schenkelwahl)
# stehen in der dataclass Parameter weiter unten.
# ============================================================================
STEP_DATEI = HIER / "PEM-Referenzstator-Aussen.STEP"

from build123d import (
    Align, Box, Cylinder, Kind, Plane, Pos, Rot,
    ExportSVG, export_step, export_stl, import_step, offset, section, extrude,
)


# ----------------------------------------------------------------------------
# 1. Parameter
# ----------------------------------------------------------------------------
@dataclass
class Parameter:
    """Alle Groessen an einer Stelle -- so bleibt der Rest des Skripts sauber."""

    # --- Grundkoerper (konstant, unabhaengig vom Hairpin) ---
    dicke: float = 5.0          # Z: Plattendicke
    laenge: float = 30.0        # Y: Langkante, Bohrung <-> Wirkkontur
    breite: float = 15.0        # X: Plattenbreite

    # --- Lagerbohrung ---
    bohrung_d: float = 6.0
    bohrung_randabstand: float = 7.5   # Y-Abstand von der hinteren Kante

    # --- Wirkkontur (aus dem Hairpin abgeleitet) ---
    z_kontakt: float = 20.0     # Hoehe im Hairpin, auf der die Klinke greift
    z_delta: float = 10.0       # 2. Schnitthoehe fuer die Neigungsmessung
    spiel: float = 0.20         # einseitiges Spiel Kontur <-> Hairpin
    eingriffstiefe: float = 3.0 # wie tief der Pin in die Nut eintaucht
    leg: str = "links"          # "links" | "rechts" | "groesste"
    kontur_drehung: float | None = None  # None = automatisch aus Schraenkung

    # --- Fertigung / Pruefung ---
    min_wandstaerke: float = 1.6      # 4 x Duesendurchmesser 0.4 mm
    haltekraft: float = 5.0           # N, fuer Flaechenpressung
    reibwert: float = 0.30            # Kunststoff / Kupferlack


# ----------------------------------------------------------------------------
# 2. Feature-Extraktion aus dem Hairpin
# ----------------------------------------------------------------------------
def kontur_extrahieren(pin, p: Parameter):
    """Schneidet den Hairpin horizontal und liefert die Kontur des Wirkschenkels.

    Rueckgabe: (face, kennwerte-dict)
    Die Kontur liegt anschliessend im Ursprung der XY-Ebene.
    """
    def schnitt(z):
        faces = section(pin, section_by=Plane.XY.offset(z)).faces()
        if not faces:
            raise ValueError(f"Kein Material auf z = {z} mm. z_kontakt pruefen.")
        return faces

    faces = schnitt(p.z_kontakt)

    # Schenkelauswahl -- ein Hairpin liefert i.d.R. zwei Querschnitte
    if p.leg == "groesste" or len(faces) == 1:
        face = max(faces, key=lambda f: f.area)
    elif p.leg == "links":
        face = min(faces, key=lambda f: f.center().X)
    elif p.leg == "rechts":
        face = max(faces, key=lambda f: f.center().X)
    else:
        raise ValueError(f"Unbekannte Schenkelwahl: {p.leg}")

    c1 = face.center()
    bb = face.bounding_box()

    # --- Zwei-Schnitt-Trick: Neigung des Schenkels ueber die Hoehe ---
    faces2 = schnitt(p.z_kontakt + p.z_delta)
    c2 = min(faces2, key=lambda f: (f.center().X - c1.X) ** 2 + (f.center().Y - c1.Y) ** 2).center()
    drift = math.hypot(c2.X - c1.X, c2.Y - c1.Y)
    alpha = math.degrees(math.atan2(drift, p.z_delta))

    kennwerte = {
        "n_querschnitte": len(faces),
        "flaeche_mm2": round(face.area, 3),
        "kontur_breite_x": round(bb.size.X, 3),
        "kontur_breite_y": round(bb.size.Y, 3),
        "schwerpunkt": [round(c1.X, 3), round(c1.Y, 3)],
        "neigungswinkel_deg": round(alpha, 2),
    }

    # Kontur in den Ursprung schieben (Z wird spaeter neu gesetzt)
    face = Pos(-c1.X, -c1.Y, -c1.Z) * face
    return face, kennwerte


# ----------------------------------------------------------------------------
# 3. Geometrieerzeugung
# ----------------------------------------------------------------------------
def klinke_bauen(kontur, kw: dict, p: Parameter):
    """Grundkoerper + Lagerbohrung - Wirkkontur."""

    # --- Grundkoerper: X = Breite (zentriert), Y = 0..Laenge, Z = 0..Dicke ---
    koerper = Box(
        p.breite, p.laenge, p.dicke,
        align=(Align.CENTER, Align.MIN, Align.MIN),
    )

    # --- Lagerbohrung, Achse parallel Z ---
    bohrung = Pos(0, p.bohrung_randabstand, -1) * Cylinder(
        p.bohrung_d / 2, p.dicke + 2, align=(Align.CENTER, Align.CENTER, Align.MIN)
    )
    koerper -= bohrung

    # --- Wirkkontur aufbereiten ---
    dreh = p.kontur_drehung
    if dreh is None:
        dreh = 0.0   # Platzhalter: hier koennte die Schraenkung kompensiert werden

    # Neigung ueber die Plattendicke -> Zusatzspiel, sonst klemmt der Pin
    zusatz = math.tan(math.radians(kw["neigungswinkel_deg"])) * p.dicke
    spiel_ges = p.spiel + zusatz

    nut = offset(kontur, amount=spiel_ges, kind=Kind.ARC)
    nut = Rot(0, 0, dreh) * nut

    # Positionieren: der Nutgrund liegt "eingriffstiefe" hinter der Vorderkante,
    # die Kontur ragt vorne ueber die Kante hinaus -> offene Nut statt Sackloch
    halbtiefe = kw["kontur_breite_y"] / 2 + spiel_ges
    y_nut = p.laenge - p.eingriffstiefe + halbtiefe
    nut = Pos(0, y_nut, -1) * nut
    nut = extrude(nut, amount=p.dicke + 2)

    klinke = koerper - nut

    kw = dict(kw)
    kw["spiel_gesamt_mm"] = round(spiel_ges, 3)
    kw["y_nutmitte_mm"] = round(y_nut, 3)
    return klinke, kw


# ----------------------------------------------------------------------------
# 4. Validierung -- liefert die Zahlen fuers Pruefprotokoll
# ----------------------------------------------------------------------------
def pruefen(klinke, kw: dict, p: Parameter) -> list[dict]:
    checks: list[dict] = []

    def add(name, wert, grenze, ok, einheit=""):
        checks.append({
            "kriterium": name, "wert": wert, "grenze": grenze,
            "einheit": einheit, "status": "OK" if ok else "NICHT ERFUELLT",
        })

    # a) Restwand zwischen Nutgrund und Lagerbohrung
    nutgrund_y = p.laenge - p.eingriffstiefe
    restwand = nutgrund_y - (p.bohrung_randabstand + p.bohrung_d / 2)
    add("Restwand Nutgrund <-> Lagerbohrung", round(restwand, 2),
        p.min_wandstaerke, restwand >= p.min_wandstaerke, "mm")

    # b) Seitliche Restwand
    nut_halbbreite = kw["kontur_breite_x"] / 2 + kw["spiel_gesamt_mm"]
    seitenwand = p.breite / 2 - nut_halbbreite
    add("Seitliche Restwand", round(seitenwand, 2),
        p.min_wandstaerke, seitenwand >= p.min_wandstaerke, "mm")

    # c) Selbsthemmung am geneigten Schenkel
    alpha = kw["neigungswinkel_deg"]
    add("Selbsthemmung tan(alpha) <= mu", round(math.tan(math.radians(alpha)), 3),
        p.reibwert, math.tan(math.radians(alpha)) <= p.reibwert, "-")

    # d) Flaechenpressung auf den Hairpin
    kontaktflaeche = p.eingriffstiefe * p.dicke
    pressung = p.haltekraft / kontaktflaeche
    add("Flaechenpressung auf Hairpin", round(pressung, 3), 5.0, pressung <= 5.0, "N/mm2")

    # e) Volumenplausibilitaet (Nut hat wirklich Material entfernt)
    v_soll = p.breite * p.laenge * p.dicke
    abtrag = v_soll - klinke.volume
    add("Materialabtrag gesamt", round(abtrag, 1), 0.0, abtrag > 0, "mm3")

    return checks


# ----------------------------------------------------------------------------
# 5. Ausgabe
# ----------------------------------------------------------------------------
def exportieren(klinke, kw, checks, p: Parameter, stem: str, outdir: Path):
    outdir.mkdir(parents=True, exist_ok=True)

    export_step(klinke, str(outdir / f"{stem}.step"))
    export_stl(klinke, str(outdir / f"{stem}.stl"))
    try:                            # Draufsicht fuer die Dokumentation
        svg = ExportSVG()
        quer = section(klinke, section_by=Plane.XY.offset(p.dicke / 2))
        svg.add_shape(Pos(0, 0, -p.dicke / 2) * quer)
        svg.write(str(outdir / f"{stem}_draufsicht.svg"))
    except Exception as e:          # SVG ist Komfort, kein Muss
        print(f"  (SVG uebersprungen: {e})")

    report = {"parameter": asdict(p), "kennwerte": kw, "pruefung": checks}
    (outdir / f"{stem}_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8"
    )

    zeilen = [f"# Pruefprotokoll {stem}", "", "## Kennwerte aus dem Hairpin"]
    zeilen += [f"- {k}: {v}" for k, v in kw.items()]
    zeilen += ["", "## Pruefkriterien", "",
               "| Kriterium | Wert | Grenze | Status |", "|---|---|---|---|"]
    for c in checks:
        zeilen.append(
            f"| {c['kriterium']} | {c['wert']} {c['einheit']} | "
            f"{c['grenze']} {c['einheit']} | {c['status']} |"
        )
    (outdir / f"{stem}_report.md").write_text("\n".join(zeilen), encoding="utf-8")


# ----------------------------------------------------------------------------
# 6. Ablauf
# ----------------------------------------------------------------------------
def lauf(quelle: Path, p: Parameter, outdir: Path):
    """Kompletter Durchlauf: Import -> Kontur -> Geometrie -> Pruefung -> Export.

    Als eigene Funktion, damit spaetere Tests den Ablauf direkt aufrufen koennen,
    ohne ueber die Kommandozeile zu gehen.
    Rueckgabe: (klinke, kennwerte, checks, stem)
    """
    print(f"[1/5] Import      : {quelle}")
    pin = import_step(str(quelle))
    bb = pin.bounding_box()
    print(f"      Bauraum     : {bb.size.X:.1f} x {bb.size.Y:.1f} x {bb.size.Z:.1f} mm")

    print(f"[2/5] Kontur      : Schnitt auf z = {p.z_kontakt} mm, Schenkel '{p.leg}'")
    kontur, kw = kontur_extrahieren(pin, p)
    print(f"      Querschnitt : {kw['flaeche_mm2']} mm2, "
          f"{kw['kontur_breite_x']} x {kw['kontur_breite_y']} mm")
    print(f"      Neigung     : {kw['neigungswinkel_deg']} Grad")

    print("[3/5] Geometrie   : Grundkoerper - Bohrung - Wirkkontur")
    klinke, kw = klinke_bauen(kontur, kw, p)

    print("[4/5] Pruefung    :")
    checks = pruefen(klinke, kw, p)
    for c in checks:
        flag = "  ok " if c["status"] == "OK" else " !!! "
        print(f"     {flag} {c['kriterium']:<38} {c['wert']:>9} {c['einheit']}"
              f"  (Grenze {c['grenze']})")

    stem = "klinke_" + quelle.stem
    print(f"[5/5] Export      : {outdir}/{stem}.step | .stl | _report.md")
    exportieren(klinke, kw, checks, p, stem, outdir)

    if any(c["status"] != "OK" for c in checks):
        print("\nACHTUNG: mindestens ein Kriterium verletzt -- Parameter anpassen.")

    return klinke, kw, checks, stem


def main():
    ap = argparse.ArgumentParser(description="Sperrklinken-Generator")
    ap.add_argument("step", nargs="?", default=None,
                    help="STEP-Datei des Hairpins (Standard: STEP_DATEI im Skriptkopf)")
    ap.add_argument("--z", type=float, help="Kontakthoehe am Hairpin [mm]")
    ap.add_argument("--spiel", type=float, help="einseitiges Spiel [mm]")
    ap.add_argument("--tiefe", type=float, help="Eingriffstiefe [mm]")
    ap.add_argument("--leg", choices=["links", "rechts", "groesste"])
    ap.add_argument("--out", default=None, help="Ausgabeordner")
    a = ap.parse_args()

    # --- Pfade robust aufloesen, egal aus welchem Verzeichnis gestartet wird ---
    quelle = Path(a.step if a.step else STEP_DATEI).expanduser()
    if not quelle.is_absolute():
        quelle = (Path.cwd() / quelle) if (Path.cwd() / quelle).exists() else HIER / quelle
    if not quelle.exists():
        raise SystemExit(
            f"STEP-Datei nicht gefunden: {quelle}\n"
            f"   -> STEP_DATEI im Kopf von {Path(__file__).name} anpassen,\n"
            f"      oder den Pfad beim Aufruf mitgeben."
        )
    outdir = Path(a.out).expanduser() if a.out else HIER / "out"

    p = Parameter()
    if a.z is not None:     p.z_kontakt = a.z
    if a.spiel is not None: p.spiel = a.spiel
    if a.tiefe is not None: p.eingriffstiefe = a.tiefe
    if a.leg:               p.leg = a.leg

    lauf(quelle, p, outdir)


if __name__ == "__main__":
    main()
