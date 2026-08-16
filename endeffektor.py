"""
endeffektor.py -- Parametrischer Generator fuer den Hairpin-Endeffektor.

Aufbau des Bauteils (zwei klar getrennte Bereiche):

    Z = 10..16   Grundplatte mit Klemmbereich   -> FEST, 1:1 aus der Referenz
    Z =  0..10   zulaufende Wirkflaeche (Keil)  -> PARAMETRISCH aus dem Hairpin

Die feste Haelfte wird nicht nachmodelliert, sondern aus endeffector.step
uebernommen und beim Laden gegen die vermessenen Referenzmasse geprueft.
Begruendung siehe referenz_pruefen(): der Klemmbereich besteht aus 151 ebenen
Flaechen, gestuftem Umriss, Senkung und Eckverrundungen -- ein Nachbau waere
viel Arbeit und jede Abweichung eine neue Fehlerquelle, ohne dass ein einziges
Mass vom Hairpin abhinge.

Benoetigt nur:  pip install build123d
Kein externes CAD-Programm.

Aufruf:
    python3 endeffektor.py
    python3 endeffektor.py --hairpin pin_v3.step --spiel 0.25
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, asdict
from pathlib import Path

HIER = Path(__file__).resolve().parent

# ============================================================================
# >>> HIER ANPASSEN <<<  Ein- und Ausgabedateien
# ============================================================================
REFERENZ_STEP = HIER / "endeffector.step"      # manuell konstruierte Vorlage
HAIRPIN_STEP = HIER / "hairpin_test.step"      # Hairpin, an den angepasst wird
# ============================================================================

from build123d import (
    Box, Compound, GeomType, Keep, Plane, Pos,
    ExportSVG, export_step, export_stl, import_step, section, split,
)
from OCP.BRepAdaptor import BRepAdaptor_Surface   # kommt mit build123d mit


# ----------------------------------------------------------------------------
# 1. Referenzmasse -- am Modell endeffector.step gemessen und verifiziert
# ----------------------------------------------------------------------------
@dataclass(frozen=True)
class Referenz:
    """Festmasse des Klemmbereichs. Alle Werte gegen die STEP-Datei geprueft.

    Achtung bei den Bohrungen: die Zahlen sind ACHSLAGEN, nicht Kantenlagen.
    """

    # --- Lage der Datei im Raum ---
    versatz: tuple = (149.092, 0.0, -13.0)   # bringt die Referenz in den Ursprung

    # --- Aussenmasse ---
    bauraum: tuple = (30.102, 18.000, 16.000)
    volumen: float = 2222.56

    # --- Grundplatte ---
    platte_z: tuple = (10.0, 16.0)     # 6 mm dick
    platte_y: tuple = (-9.0, 9.0)      # 18 mm breit
    trennebene_z: float = 10.0         # Grenze fest <-> parametrisch

    # --- Hauptbohrung, Achse parallel Z ---
    hauptbohrung_d: float = 6.2
    hauptbohrung_xy: tuple = (16.692, 0.0)

    # --- Klemmschlitz: von der Hauptbohrung nach +X bis zur hinteren Kante ---
    schlitz_halbbreite: float = 0.51   # Schlitz liegt zwischen Y = -0.51 .. +0.51
    schlitz_x: tuple = (19.75, 30.102)

    # --- EINE Klemmschraube, Achse parallel Y, quer durch den Schlitz ---
    schraube_d: float = 3.2
    schraube_xz: tuple = (23.792, 13.0)
    senkung_d: float = 6.0
    senkung_y: tuple = (-6.0, -4.0)    # Senkung nur auf der -Y-Seite

    # --- senkrechte Eckverrundungen der Platte (Achse parallel Z) ---
    eckradien: tuple = (1.0, 2.0)

    # --- Flaechenzaehlung des Gesamtkoerpers (PLANE, BSPLINE, CYLINDER) ---
    flaechen: tuple = (151, 75, 9)


REF = Referenz()


# ----------------------------------------------------------------------------
# 2. Parameter -- alles, was der Anwender einstellt
# ----------------------------------------------------------------------------
@dataclass
class Parameter:
    """Einstellgroessen. Die Festmasse stehen in Referenz, nicht hier."""

    # --- Wirkflaeche (wird in Schritt 2 aus dem Hairpin abgeleitet) ---
    spiel: float = 0.20         # einseitiges Spiel Wirkflaeche <-> Hairpin

    # --- Uebergangsloesung, bis die Wirkflaeche generiert wird ---
    keil_aus_referenz: bool = True   # True = Originalkeil anhaengen

    # --- Pruefung ---
    mass_toleranz: float = 0.02      # zulaessige Abweichung von den Referenzmassen
    volumen_toleranz: float = 0.50   # mm3, deckt Bool-/Vernetzungsrauschen ab


# ----------------------------------------------------------------------------
# 3. Referenz laden und verifizieren
# ----------------------------------------------------------------------------
def _zylinder(shape) -> list[dict]:
    """Alle Zylinderflaechen mit Radius, Achsrichtung und Achspunkt."""
    treffer = []
    for f in shape.faces():
        if f.geom_type != GeomType.CYLINDER:
            continue
        cyl = BRepAdaptor_Surface(f.wrapped).Cylinder()
        richtung, punkt = cyl.Axis().Direction(), cyl.Axis().Location()
        treffer.append({
            "r": cyl.Radius(),
            "richtung": (richtung.X(), richtung.Y(), richtung.Z()),
            "punkt": (punkt.X(), punkt.Y(), punkt.Z()),
            "bbox": f.bounding_box(),
        })
    return treffer


def referenz_laden(pfad: Path = None, r: Referenz = REF):
    """Referenz einlesen und in den Ursprung schieben.

    Die Datei liegt in Fusion-Koordinaten weit ausserhalb des Ursprungs --
    ohne diesen Versatz stimmt keine einzige der Konstanten unten.
    """
    quelle = Path(pfad) if pfad else REFERENZ_STEP
    if not quelle.exists():
        raise SystemExit(
            f"Referenzmodell nicht gefunden: {quelle}\n"
            f"   -> REFERENZ_STEP im Kopf von {Path(__file__).name} anpassen."
        )
    return Pos(*r.versatz) * import_step(str(quelle))


def referenz_pruefen(eff, p: Parameter, r: Referenz = REF) -> list[dict]:
    """Prueft die Referenzdatei gegen die hinterlegten Masse.

    Sinn: das Skript haengt an einer fremden Datei. Wird die in Fusion
    geaendert (anderer Versatz, andere Bohrung), sollen die Konstanten hier
    nicht stillschweigend falsch werden, sondern auffallen.
    """
    checks: list[dict] = []

    def add(name, wert, soll, tol, einheit=""):
        ok = abs(wert - soll) <= tol
        checks.append({
            "kriterium": name, "wert": round(wert, 3), "soll": round(soll, 3),
            "toleranz": tol, "einheit": einheit,
            "status": "OK" if ok else "ABWEICHUNG",
        })

    bb = eff.bounding_box()

    # a) Lage und Aussenmasse
    add("Versatz korrekt (X-Minimum bei 0)", bb.min.X, 0.0, p.mass_toleranz, "mm")
    add("Bauraum X", bb.size.X, r.bauraum[0], p.mass_toleranz, "mm")
    add("Bauraum Y", bb.size.Y, r.bauraum[1], p.mass_toleranz, "mm")
    add("Bauraum Z", bb.size.Z, r.bauraum[2], p.mass_toleranz, "mm")
    add("Volumen", eff.volume, r.volumen, p.volumen_toleranz, "mm3")

    # b) Grundplatte
    add("Plattenbreite Y", bb.size.Y, r.platte_y[1] - r.platte_y[0], p.mass_toleranz, "mm")
    add("Plattenoberkante Z", bb.max.Z, r.platte_z[1], p.mass_toleranz, "mm")

    zyl = _zylinder(eff)

    # c) Hauptbohrung: Achse parallel Z, Radius und Lage
    haupt = [z for z in zyl
             if abs(abs(z["richtung"][2]) - 1) < 1e-6
             and abs(z["r"] - r.hauptbohrung_d / 2) < 0.05]
    add("Hauptbohrung gefunden (Anzahl Flaechen)", len(haupt), 1, 0, "")
    if haupt:
        add("Hauptbohrung Durchmesser", 2 * haupt[0]["r"], r.hauptbohrung_d,
            p.mass_toleranz, "mm")
        add("Hauptbohrung Achse X", haupt[0]["punkt"][0], r.hauptbohrung_xy[0],
            p.mass_toleranz, "mm")
        add("Hauptbohrung Achse Y", haupt[0]["punkt"][1], r.hauptbohrung_xy[1],
            p.mass_toleranz, "mm")

    # d) Klemmschraube: Achse parallel Y, Radius und Lage
    schraube = [z for z in zyl
                if abs(abs(z["richtung"][1]) - 1) < 1e-6
                and abs(z["r"] - r.schraube_d / 2) < 0.05]
    add("Klemmschraube gefunden (Anzahl Flaechen)", len(schraube), 2, 0, "")
    if schraube:
        add("Klemmschraube Durchmesser", 2 * schraube[0]["r"], r.schraube_d,
            p.mass_toleranz, "mm")
        add("Klemmschraube Achse X", schraube[0]["punkt"][0], r.schraube_xz[0],
            p.mass_toleranz, "mm")
        add("Klemmschraube Achse Z", schraube[0]["punkt"][2], r.schraube_xz[1],
            p.mass_toleranz, "mm")

    # e) Senkung fuer den Schraubenkopf
    senkung = [z for z in zyl
               if abs(abs(z["richtung"][1]) - 1) < 1e-6
               and abs(z["r"] - r.senkung_d / 2) < 0.05]
    add("Senkung gefunden (Anzahl Flaechen)", len(senkung), 2, 0, "")

    # f) Klemmschlitz: zwei parallele Waende bei Y = +-schlitz_halbbreite
    wand = [f for f in eff.faces()
            if f.geom_type == GeomType.PLANE
            and abs(abs(f.center().Y) - r.schlitz_halbbreite) < p.mass_toleranz
            and f.bounding_box().max.X > r.schlitz_x[1] - 0.1]
    add("Klemmschlitz: Waende bis zur Hinterkante", len(wand), 2, 0, "")

    # g) Flaechenzaehlung -- schlaegt an, wenn die Referenz umkonstruiert wurde
    typen = [f.geom_type for f in eff.faces()]
    for name, typ, soll in (("ebene", GeomType.PLANE, r.flaechen[0]),
                            ("B-Spline", GeomType.BSPLINE, r.flaechen[1]),
                            ("zylindrische", GeomType.CYLINDER, r.flaechen[2])):
        add(f"Anzahl {name} Flaechen", typen.count(typ), soll, 0, "")

    return checks


# ----------------------------------------------------------------------------
# 4. Geometrieerzeugung
# ----------------------------------------------------------------------------
def grundkoerper(eff, r: Referenz = REF):
    """Trennt die feste Haelfte (Grundplatte samt Klemmbereich) heraus.

    Getrennt wird an Z = trennebene_z. Alles darueber ist fest, alles darunter
    ist die Wirkflaeche und wird spaeter neu erzeugt.
    """
    return split(eff, bisect_by=Plane.XY.offset(r.trennebene_z), keep=Keep.TOP)


def referenzkeil(eff, r: Referenz = REF):
    """Der Originalkeil aus der Referenz -- Uebergangsloesung und Vergleichsmass.

    Solange die Wirkflaeche noch nicht aus dem Hairpin erzeugt wird, haengen
    wir diesen Keil an, damit ein vollstaendiger Koerper herauskommt. Spaeter
    dient er als Vergleich fuer die generierte Variante.
    """
    return split(eff, bisect_by=Plane.XY.offset(r.trennebene_z), keep=Keep.BOTTOM)


def effektor_bauen(eff, p: Parameter, r: Referenz = REF):
    """Setzt den Endeffektor zusammen. Rueckgabe: (koerper, kennwerte)."""
    platte = grundkoerper(eff, r)
    kw = {
        "grundkoerper_volumen_mm3": round(platte.volume, 2),
        "grundkoerper_solids": len(platte.solids()),
    }

    if p.keil_aus_referenz:
        keil = referenzkeil(eff, r)
        koerper = platte + keil
        kw["keil_quelle"] = "Referenz (Platzhalter)"
        kw["keil_volumen_mm3"] = round(keil.volume, 2)
    else:
        koerper = platte
        kw["keil_quelle"] = "keiner"
        kw["keil_volumen_mm3"] = 0.0

    kw["gesamt_volumen_mm3"] = round(koerper.volume, 2)
    kw["gesamt_solids"] = len(koerper.solids())
    bb = koerper.bounding_box()
    kw["bauraum"] = [round(bb.size.X, 3), round(bb.size.Y, 3), round(bb.size.Z, 3)]
    return koerper, kw


# ----------------------------------------------------------------------------
# 5. Pruefung des erzeugten Koerpers
# ----------------------------------------------------------------------------
def koerper_pruefen(koerper, kw: dict, p: Parameter, r: Referenz = REF) -> list[dict]:
    checks: list[dict] = []

    def add(name, wert, soll, tol, einheit=""):
        ok = abs(wert - soll) <= tol
        checks.append({
            "kriterium": name, "wert": round(wert, 3), "soll": round(soll, 3),
            "toleranz": tol, "einheit": einheit,
            "status": "OK" if ok else "ABWEICHUNG",
        })

    bb = koerper.bounding_box()
    add("Ergebnis ist EIN Solid", len(koerper.solids()), 1, 0, "")
    add("Plattendicke Z", bb.max.Z - r.trennebene_z,
        r.platte_z[1] - r.platte_z[0], p.mass_toleranz, "mm")
    add("Plattenbreite Y", bb.size.Y, r.platte_y[1] - r.platte_y[0],
        p.mass_toleranz, "mm")

    if p.keil_aus_referenz:
        # Nichts darf beim Trennen und Zusammensetzen verloren gegangen sein
        add("Volumen = Referenzvolumen", koerper.volume, r.volumen,
            p.volumen_toleranz, "mm3")
    else:
        add("Volumen = Referenz ohne Keil", koerper.volume,
            kw["grundkoerper_volumen_mm3"], p.volumen_toleranz, "mm3")

    return checks


# ----------------------------------------------------------------------------
# 6. Ausgabe
# ----------------------------------------------------------------------------
def exportieren(koerper, kw, checks_ref, checks_koerper, p, stem, outdir: Path,
                r: Referenz = REF):
    outdir.mkdir(parents=True, exist_ok=True)

    export_step(koerper, str(outdir / f"{stem}.step"))
    export_stl(koerper, str(outdir / f"{stem}.stl"))

    # Zwei Schnitte fuer die Dokumentation. ExportSVG zeichnet nur, was in der
    # XY-Ebene liegt -- der Laengsschnitt muss also erst dorthin gedreht werden.
    for name, ebene, in_xy_drehen in (
        ("draufsicht", Plane.XY.offset(13.0), Pos(0, 0, -13.0)),
        ("laengsschnitt", Plane(origin=(0, 0, 0), z_dir=(0, 1, 0)), Rot(90, 0, 0)),
    ):
        try:
            svg = ExportSVG()
            svg.add_shape(in_xy_drehen * section(koerper, section_by=ebene))
            svg.write(str(outdir / f"{stem}_{name}.svg"))
        except Exception as e:
            print(f"  (SVG {name} uebersprungen: {e})")

    report = {
        "referenz": asdict(r),
        "parameter": asdict(p),
        "kennwerte": kw,
        "pruefung_referenz": checks_ref,
        "pruefung_koerper": checks_koerper,
    }
    (outdir / f"{stem}_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8"
    )

    zeilen = [f"# Pruefprotokoll {stem}", "", "## Kennwerte"]
    zeilen += [f"- {k}: {v}" for k, v in kw.items()]
    for titel, liste in (("Verifikation der Referenzdatei", checks_ref),
                         ("Pruefung des erzeugten Koerpers", checks_koerper)):
        zeilen += ["", f"## {titel}", "",
                   "| Kriterium | Wert | Soll | Toleranz | Status |",
                   "|---|---|---|---|---|"]
        for c in liste:
            zeilen.append(
                f"| {c['kriterium']} | {c['wert']} {c['einheit']} | "
                f"{c['soll']} {c['einheit']} | {c['toleranz']} | {c['status']} |"
            )
    (outdir / f"{stem}_report.md").write_text("\n".join(zeilen), encoding="utf-8")


def tabelle_zeigen(titel: str, checks: list[dict]) -> None:
    print(f"      {titel}")
    for c in checks:
        flag = "  ok " if c["status"] == "OK" else " !!! "
        print(f"     {flag} {c['kriterium']:<42} {c['wert']:>10} {c['einheit']:<4}"
              f" (Soll {c['soll']})")


# ----------------------------------------------------------------------------
# 7. Ablauf
# ----------------------------------------------------------------------------
def lauf(referenz_pfad: Path, p: Parameter, outdir: Path, stem: str = "endeffektor"):
    """Kompletter Durchlauf. Rueckgabe: (koerper, kennwerte, checks_ref, checks_koerper)."""
    print(f"[1/4] Referenz    : {referenz_pfad}")
    eff = referenz_laden(referenz_pfad)
    bb = eff.bounding_box()
    print(f"      Bauraum     : {bb.size.X:.3f} x {bb.size.Y:.3f} x {bb.size.Z:.3f} mm,"
          f" {eff.volume:.2f} mm3")

    print("[2/4] Verifikation der Referenzmasse:")
    checks_ref = referenz_pruefen(eff, p)
    tabelle_zeigen("", checks_ref)

    print(f"[3/4] Geometrie   : Trennung an Z = {REF.trennebene_z} mm")
    koerper, kw = effektor_bauen(eff, p)
    print(f"      Grundkoerper: {kw['grundkoerper_volumen_mm3']} mm3")
    print(f"      Keil        : {kw['keil_volumen_mm3']} mm3 ({kw['keil_quelle']})")
    checks_koerper = koerper_pruefen(koerper, kw, p)
    tabelle_zeigen("Pruefung des erzeugten Koerpers:", checks_koerper)

    print(f"[4/4] Export      : {outdir}/{stem}.step | .stl | _report.md")
    exportieren(koerper, kw, checks_ref, checks_koerper, p, stem, outdir)

    alle = checks_ref + checks_koerper
    if any(c["status"] != "OK" for c in alle):
        print("\nACHTUNG: mindestens ein Kriterium weicht ab -- Referenz pruefen.")
    return koerper, kw, checks_ref, checks_koerper


def main():
    ap = argparse.ArgumentParser(description="Endeffektor-Generator")
    ap.add_argument("--referenz", default=None, help="Referenz-STEP (Standard: REFERENZ_STEP)")
    ap.add_argument("--spiel", type=float, help="einseitiges Spiel [mm]")
    ap.add_argument("--ohne-keil", action="store_true",
                    help="nur den festen Grundkoerper erzeugen")
    ap.add_argument("--out", default=None, help="Ausgabeordner")
    a = ap.parse_args()

    p = Parameter()
    if a.spiel is not None:
        p.spiel = a.spiel
    if a.ohne_keil:
        p.keil_aus_referenz = False

    referenz = Path(a.referenz).expanduser() if a.referenz else REFERENZ_STEP
    outdir = Path(a.out).expanduser() if a.out else HIER / "out"
    lauf(referenz, p, outdir)


if __name__ == "__main__":
    main()
