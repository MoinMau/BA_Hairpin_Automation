"""
endeffektor.py -- Parametrischer Generator fuer die beiden Hairpin-Endeffektoren.

Die Hairpins haengen als Buendel auf einer Stange, Kopf an Kopf. Zwei getrennt
auf- und abfahrende Stangen tragen je einen Endeffektor:

    HALTER     haelt das Buendel auf, damit die Reihe nicht nachrutscht.
               Seine Stirnseite bildet die Kopf-Knickbiegung ab -- eine reine
               Anlageflaeche, keine Tasche, der Kopf liegt buendig an. Sie ist
               bewusst breiter als die Biegung, damit die Koepfe stabil
               abgestuetzt werden. Unter der Platte laeuft nur eine schmale
               Stirnleiste weiter nach unten, nicht der ganze Koerper.

    SCHNEIDER  faehrt zwischen zwei Koepfe und vereinzelt genau einen.
               Unter der Grundplatte sitzt ein einseitig angeschliffener Keil:
               Vorderflaeche senkrecht (folgt der Knickkontur), Rueckflaeche
               um 'keilwinkel' angestellt, unten eine schmale Schneidfase.
               Beim Herabfahren drueckt die Schraege den naechsten Pin weg.

Beide teilen sich dieselbe Grundplatte: Wellenbohrung, Klemmschlitz,
Klemmschraube mit Senkung und Sechskanttasche fuer die Mutter.

Koordinaten
-----------
    X = 0   der am weitesten vorstehende Punkt der Anlagekante; von dort
            laeuft das Bauteil ueber 'laenge' nach hinten. Alle X-Masse
            (Bohrung, Schraube) beziehen sich auf diesen Punkt.
    Y = 0   Mittelebene, die Knickbiegung wird darauf zentriert.
    Z       'unterkante_z' ist die Trennebene Platte/Unterbau; darunter
            haengt Keil bzw. Stirnleiste, darueber die Grundplatte.

Die Stirnform kommt vollstaendig aus dem Hairpin -- ohne STEP-Datei laeuft
nichts. Die Kontur entsteht als Draufsicht-Projektion des Kopfbereichs.

Benoetigt nur:  pip install build123d
Kein externes CAD-Programm.

Aufruf:
    python3 endeffektor.py --hairpin PEM-Referenzstator-Aussen.STEP
    python3 endeffektor.py --variante halter --hairpin PEM-Referenzstator-Aussen.STEP
    python3 endeffektor.py --variante schneider --keilwinkel 25 --eintauchtiefe 4
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass, asdict, fields
from pathlib import Path

HIER = Path(__file__).resolve().parent

from build123d import (
    Align, Axis, Box, Cylinder, Plane, Pos, Rectangle, RegularPolygon, Rot, Vector,
    ExportSVG, export_step, export_stl, extrude, fillet, import_step, section,
)


# ----------------------------------------------------------------------------
# 1. Parameter
# ----------------------------------------------------------------------------
@dataclass
class ParameterBasis:
    """Was beide Endeffektoren gemeinsam haben.

    Die Zahlenwerte sind am Referenzmodell endeffector.step vermessen. Sie sind
    echte Parameter, keine Festmasse.

    Achtung bei den Bohrungen: die Werte sind ACHSLAGEN, nicht Kantenlagen,
    und zaehlen ab dem vorstehendsten Punkt der Anlagekante (X = 0).
    """

    # --- Grundplatte ---
    laenge: float = 30.100      # X: Anlagekante (0) bis Hinterkante
    breite: float = 18.000      # Y: symmetrisch um Y = 0; wird i.d.R. abgeleitet
    dicke: float = 8.000        # Z: Vorlage hatte 6.0 -- hier +2 mm
    unterkante_z: float = 10.0  # Trennebene Platte / Unterbau

    # --- Hauptbohrung fuer die Welle, Achse parallel Z ---
    bohrung_d: float = 6.2
    bohrung_x: float = 13.600   # Achslage; Y liegt auf der Mittelebene

    # --- Klemmschlitz: von der Hauptbohrung nach +X bis zur Hinterkante ---
    schlitz_breite: float = 1.02

    # --- Klemmschraube, Achse parallel Y, quer durch den Schlitz ---
    schraube_d: float = 3.2
    schraube_x: float = 20.700     # Achslage; Z liegt auf halber Plattendicke
    senkung_d: float = 6.0
    senkung_tiefe: float = 2.0     # ab der -Y-Aussenflaeche nach innen

    # --- Sechskant-Aussparung fuer die Mutter, +Y-Seite ---
    mutter_sw: float = 5.5         # Schluesselweite M3 (DIN 934 / ISO 4032)
    mutter_spiel: float = 0.2      # Einlegespiel auf die Schluesselweite
    mutter_tiefe: float = 2.6      # Mutterhoehe M3 = 2.4 mm + Zugabe
    mutter_drehung: float = 0.0    # 0 = Schluesselflaechen oben/unten

    # --- Kanten ---
    eckradius: float = 2.0         # nur die HINTEREN senkrechten Ecken
    kantenradius: float = 0.5      # Bruch der Oberkanten; 0 = scharf lassen

    # --- Kopfkontur aus dem Hairpin ---
    kopf_spiel: float = 0.20        # Abstand Stirnflaeche <-> Hairpin
    kopf_y: float = 0.0             # Kontur quer verschieben
    kopf_schnitt_dz: float = 1.5    # Hoehenschritt der Projektionsschnitte
    # Stuetzstellen der Anlagekante. Daraus wird auch der Nullpunkt in X
    # bestimmt -- der vorderste Punkt der Kante. Zu grob abgetastet rutscht
    # dieser Punkt zwischen zwei Stuetzstellen durch und das ganze Bauteil
    # sitzt um den Fehlbetrag versetzt. 101 Stellen kosten rund eine Sekunde
    # und liegen beim Referenzstator auf 0.004 mm genau.
    kopf_abtastung: int = 101

    # --- Plattenbreite aus der Biegung ableiten ---
    kopf_breite_ableiten: bool = True
    kopf_suchbreite: float = 40.0     # so weit wird nach der Biegung gesucht
    kopf_kern_schwelle: float = 0.15  # Anteil der Maximalkruemmung fuer den Kern
    kopf_auslauf: float = 0.9         # Zuschlag je Seite, in Kernbreiten
    # Die Biegung gibt die Mindestbreite vor; wer mehr Anlageflaeche will,
    # streckt sie hiermit. Begrenzt wird trotzdem auf das, was die Kontur
    # ueberhaupt hergibt -- breiter als der Kopf geht nicht.
    breite_faktor: float = 1.0

    # --- Fertigung / Pruefung ---
    min_wandstaerke: float = 1.6   # 4 x Duesendurchmesser
    duesen_d: float = 0.4          # Bambu Lab Standarddüse

    # --- abgeleitete Groessen ---
    @property
    def oberkante_z(self) -> float:
        return self.unterkante_z + self.dicke

    @property
    def mitte_z(self) -> float:
        """Hoehe der Schraubenachse -- immer auf halber Plattendicke."""
        return self.unterkante_z + self.dicke / 2

    def breite_bei(self, x: float) -> float:
        """Bauteilbreite an der Laengsstelle x.

        Ohne Taille ist sie ueberall gleich. Der Halter ueberschreibt das:
        vorne traegt die volle Stirnseite, hinten reicht der schmale Hals.
        Alles, was auf die Aussenflaeche Bezug nimmt -- Senkung, Mutterntasche,
        Restwaende -- muss diese Funktion benutzen und nicht 'breite', sonst
        sticht die Senkung neben dem Bauteil ins Leere.
        """
        return self.breite

    @property
    def klemm_breite(self) -> float:
        """Bauteilbreite auf Hoehe der Klemmschraube."""
        return self.breite_bei(self.schraube_x)

    @property
    def aussen_y(self) -> float:
        """Y der -Y-Aussenflaeche am Klemmbereich; dort taucht die Senkung ein."""
        return -self.klemm_breite / 2

    @property
    def mutter_sw_gesamt(self) -> float:
        return self.mutter_sw + self.mutter_spiel

    @property
    def unterbau_hoehe(self) -> float:
        raise NotImplementedError

    @property
    def unterkante_bauteil(self) -> float:
        return self.unterkante_z - self.unterbau_hoehe


@dataclass
class ParameterHalter(ParameterBasis):
    """Endeffektor 1 -- haelt das Buendel auf.

    Unter der Platte laeuft nur eine Stirnleiste weiter nach unten -- alles
    hinter 'anlage_tiefe' faellt weg. Ihre Stirnflaeche ist dieselbe
    Knickkontur wie die der Platte und laeuft senkrecht durch: der Kopf liegt
    ueber die ganze Hoehe buendig an. Kein Keil, kein Ruecksprung, keine
    Tasche.
    """
    anlage_hoehe: float = 10.0     # Hoehe der Stirnleiste unter der Platte
    # Wie weit die Leiste nach hinten reicht, ab X = 0 gemessen. Leer = aus
    # der Formtiefe abgeleitet, damit hinter dem tiefsten Punkt der Kontur
    # noch Wand steht.
    anlage_tiefe: float | None = None
    # Der Halter soll die Koepfe stabil abstuetzen, nicht nur die Biegung
    # abbilden -- deshalb deutlich breiter als der Schneider.
    breite_faktor: float = 2.5

    # --- Taille: hinten braucht nur die Klemmung Platz ---
    # Die breite Stirnseite wird nur vorne gebraucht, wo die Koepfe anliegen.
    # Dahinter darf das Bauteil einschnueren -- das spart Material und laesst
    # der Nachbarmechanik Luft.
    hals_breite: float | None = None   # leer = so schmal, wie die Klemmung erlaubt
    hals_ab_x: float | None = None     # leer = direkt hinter der Stirnleiste
    hals_radius: float = 3.0           # Verrundung der Schulter; 0 = harte Stufe

    @property
    def unterbau_hoehe(self) -> float:
        return self.anlage_hoehe

    def breite_bei(self, x: float) -> float:
        """Vor der Schulter die volle Stirnseite, dahinter der Hals."""
        if self.hals_breite is None or self.hals_ab_x is None:
            return self.breite
        return self.breite if x < self.hals_ab_x else self.hals_breite


@dataclass
class ParameterSchneider(ParameterBasis):
    """Endeffektor 2 -- vereinzelt durch Eintauchen zwischen zwei Koepfe.

    Der Keil ist einseitig angeschliffen: vorne senkrecht (Anlage am Pin, der
    stehenbleibt), hinten unter 'keilwinkel' (schiebt den naechsten Pin weg).
    Unten bleibt eine Schneidfase stehen -- eine wirklich scharfe Kante ist
    im FDM-Druck nicht herstellbar.

    Der Keil ist hoeher als er eintaucht: 'eintauchtiefe' ist der Arbeitshub,
    'klinge_hoehe' die gebaute Hoehe. Die Differenz ist Puffer nach oben.
    """
    klinge_hoehe: float = 10.0        # gebaute Hoehe des Keils
    eintauchtiefe: float = 5.0        # tatsaechlicher Arbeitshub
    keilwinkel: float = 20.8          # Anstellung der Rueckflaeche [Grad]
    schneide_fase: float = 0.2        # Dicke der Schneidkante unten
    klinge_breite: float | None = None  # leer = so breit wie die Platte

    # Die Platte darf den Hairpin nicht beruehren -- nur der Keil arbeitet.
    platte_ruecksprung: float = 0.3   # Stirnflaeche hinter der Keilvorderkante
    # 0 = die Stirnflaeche der Platte laeuft senkrecht hoch. Der Ruecksprung
    # allein haelt sie schon vom Hairpin weg; eine zusaetzliche Neigung kostet
    # nur Material und bringt nichts.
    platte_freiwinkel: float = 0.0    # Ruecknahme der Stirnflaeche nach oben [Grad]

    @property
    def unterbau_hoehe(self) -> float:
        return self.klinge_hoehe

    def klingendicke(self, ueber_der_schneide: float) -> float:
        """Keildicke in einer bestimmten Hoehe ueber der Schneidkante."""
        return self.schneide_fase + math.tan(math.radians(self.keilwinkel)) * ueber_der_schneide


VARIANTEN = {
    "halter": ParameterHalter,
    "schneider": ParameterSchneider,
}

# Rueckwaertskompatibel: wer "endeffektor.Parameter" schreibt, bekommt den Keil.
Parameter = ParameterSchneider


# ----------------------------------------------------------------------------
# 2. Kopfkontur -- die einzige Groesse, die aus dem Hairpin kommt
# ----------------------------------------------------------------------------
def _quer(form, z):
    """Waagerechter Querschnitt auf Hoehe z; leere Liste statt Ausnahme."""
    try:
        return section(form, section_by=Plane.XY.offset(z)).faces()
    except Exception:
        return []


def kopf_kontur(pin, p: ParameterBasis):
    """Draufsicht-Kontur der Knickbiegung, ausgerichtet und um das Spiel geweitet.

    Schaut man senkrecht von oben auf den Hairpin, laeuft der Kopf nicht gerade
    zwischen den beiden Schenkeln durch, sondern knickt in der Mitte ab. Genau
    diese Kontur formt die Stirnseite beider Endeffektoren.

    Rueckgabe: (kontur, kennwerte). Die Kontur liegt so:

        +X   Richtung Knickspitze -- der Endeffektor steht bei groesserem X
        +Y   laengs der Sehne zwischen den Schenkeln, Sehnenmitte auf Y = 0

    An welchem Ende der Kopf sitzt, wird gemessen statt angenommen: beim
    Referenzstator liegt er unten (Z = -27), beim Testpin oben (Z = +63).
    """
    bb = pin.bounding_box()
    kopf_unten = len(_quer(pin, bb.min.Z + 0.02 * bb.size.Z)) <= 1
    richtung = 1 if kopf_unten else -1
    scheitel_z = bb.min.Z if kopf_unten else bb.max.Z

    # --- Sehne: Verbindung der beiden geraden Schenkel auf halber Hoehe ---
    mitte_fl = _quer(pin, bb.center().Z)
    if len(mitte_fl) < 2:
        raise ValueError(
            "Auf halber Hoehe des Hairpins liegen keine zwei Schenkel. "
            "Ist die STEP-Datei wirklich ein einzelner Hairpin?"
        )
    schenkel = sorted((f.center().X, f.center().Y) for f in mitte_fl)
    sehne = Vector(schenkel[1][0] - schenkel[0][0],
                   schenkel[1][1] - schenkel[0][1], 0).normalized()
    sehnenmitte = Vector((schenkel[0][0] + schenkel[1][0]) / 2,
                         (schenkel[0][1] + schenkel[1][1]) / 2, 0)

    # --- Quer dazu, in Richtung der Knickspitze ---
    spitze = _quer(pin, scheitel_z + richtung * 0.4)
    if not spitze:
        raise ValueError("Am Kopfende des Hairpins liegt kein Material.")
    zum_knick = Vector(spitze[0].center().X, spitze[0].center().Y, 0) - sehnenmitte
    quer = Vector(-sehne.Y, sehne.X, 0)
    if quer.dot(zum_knick) < 0:
        quer = -quer
    system = Plane(origin=(sehnenmitte.X, sehnenmitte.Y, 0), x_dir=quer, z_dir=(0, 0, 1))

    # --- Draufsicht: waagerechte Schnitte uebereinanderlegen ---
    # Gesammelt wird nur so weit, bis die Kontur breiter als der Suchbereich
    # ist -- alles darueber hinaus kann die Stirnseite ohnehin nicht treffen.
    flaechen: list = []
    z = scheitel_z + richtung * 0.2
    while abs(z - scheitel_z) < bb.size.Z:
        roh = _quer(pin, z)
        if roh:
            eben = [Pos(0, 0, -z) * f for f in roh]
            flaechen += eben
            lokal = [system.to_local_coords(f) for f in eben]
            raender = [w for f in lokal
                       for w in (f.bounding_box().min.Y, f.bounding_box().max.Y)]
            if max(raender) - min(raender) > p.kopf_suchbreite * 1.25:
                break
        z += richtung * p.kopf_schnitt_dz
    if not flaechen:
        raise ValueError("Kopfbereich des Hairpins liefert keine Schnittflaechen.")

    kontur = system.to_local_coords(flaechen[0].fuse(*flaechen[1:]).clean())

    # --- um das Spiel aufweiten ---
    # offset_2d scheitert an dieser Kontur (OCCT liefert eine leere Form),
    # deshalb die Minkowski-Naeherung ueber verschobene Kopien.
    if p.kopf_spiel > 0:
        kopien = [Pos(p.kopf_spiel * math.cos(i * math.pi / 4),
                      p.kopf_spiel * math.sin(i * math.pi / 4), 0) * kontur
                  for i in range(8)]
        kontur = kontur.fuse(*kopien).clean()

    return kontur, {
        "kopf_liegt": "unten" if kopf_unten else "oben",
        "kopf_spannweite_mm": round(math.dist(schenkel[0], schenkel[1]), 3),
        "kopf_schnitte": len(flaechen),
    }


def kopf_anlagekante(kontur, breite: float, stuetzstellen: int) -> list:
    """Die dem Endeffektor zugewandte Seite der Kontur abtasten.

    Fuer jede Abtaststelle das GROESSTE X der Kontur. Das ist entscheidend:
    an dieser Linie beginnt das Bauteil. Nimmt man stattdessen die kleinste
    (die abgewandte Innenseite), steckt der Hairpin hinterher im Material.

    Abgetastet statt analytisch, weil die Kontur aus Freiformkanten besteht
    und ihre Aussenseite nicht als einzelne Kurve vorliegt.
    """
    werte = []
    schritt = breite / (stuetzstellen - 1)
    for i in range(stuetzstellen):
        y = -breite / 2 + i * schritt
        stueck = kontur & (Pos(0, y, 0) * Rectangle(400, schritt * 0.9 + 0.01))
        if stueck.faces():
            werte.append((round(y, 4), stueck.bounding_box().max.X))
    return werte


def kopf_biegung(kontur, p: ParameterBasis):
    """Die Knickbiegung eingrenzen und daraus die noetige Breite bestimmen.

    Die Stirnseite soll die Biegung vollstaendig zeigen, ihre beiden Endpunkte
    genau auf den Ecken. Gesucht sind also Mitte und Breite der Biegung.

    Gefunden wird sie ueber die Kruemmung der Anlagekante: im Knick aendert die
    Kante ihre Richtung um ein Vielfaches dessen, was der umgebende flache
    Bogen tut. Der so gefundene KERN ist beim Referenzstator nur gut 4 mm breit
    -- die Biegung endet aber nicht scharf, sondern laeuft allmaehlich in den
    Bogen aus. Deshalb wird der Kern um kopf_auslauf Kernbreiten je Seite
    erweitert. Wo genau die Biegung "aufhoert", ist eine Konvention, keine
    Messgroesse -- beide Werte sind daher einstellbar.

    Rueckgabe: (mitte_y, breite, kennwerte).
    """
    kante = kopf_anlagekante(kontur, p.kopf_suchbreite, 81)
    if len(kante) < 5:
        raise ValueError("Kopfkontur zu schmal fuer die Biegungssuche.")

    ys = [y for y, _ in kante]
    xs = [x for _, x in kante]
    kruemmung = []
    for i in range(1, len(kante) - 1):
        h = (ys[i + 1] - ys[i - 1]) / 2
        kruemmung.append((ys[i], abs((xs[i + 1] - 2 * xs[i] + xs[i - 1]) / (h * h))))

    hoechste = max(k for _, k in kruemmung)
    kern = [y for y, k in kruemmung if k > p.kopf_kern_schwelle * hoechste]
    if not kern:
        raise ValueError(
            "Im Hairpin-Kopf ist keine Knickbiegung erkennbar. "
            "kopf_kern_schwelle senken oder kopf_breite_ableiten abschalten."
        )

    kern_von, kern_bis = min(kern), max(kern)
    kern_breite = kern_bis - kern_von
    von = kern_von - p.kopf_auslauf * kern_breite
    bis = kern_bis + p.kopf_auslauf * kern_breite

    return (von + bis) / 2, bis - von, {
        "kopf_kern_von_mm": round(kern_von, 3),
        "kopf_kern_bis_mm": round(kern_bis, 3),
        "kopf_biegung_breite_mm": round(bis - von, 3),
    }


def kopf_mindestbreite(p: ParameterBasis) -> float:
    """Schmaler darf das Bauteil nicht werden, sonst passt der Klemmbereich nicht.

    Massgeblich ist der unguenstigste der drei Faelle: die Wellenbohrung mit
    ihren Restwaenden, und die beiden Taschen fuer Schraubenkopf und Mutter,
    die sich von beiden Seiten auf den Klemmschlitz zubewegen.
    """
    return max(
        p.bohrung_d + 2 * p.min_wandstaerke,
        p.senkung_d + 2 * p.min_wandstaerke,
        2 * (p.senkung_tiefe + p.schlitz_breite / 2 + p.min_wandstaerke),
        2 * (p.mutter_tiefe + p.schlitz_breite / 2 + p.min_wandstaerke),
    )


# ----------------------------------------------------------------------------
# 3. Stirnseite -- aus der Kontur wird der Bereich VOR dem Bauteil
# ----------------------------------------------------------------------------
def stirn_region(kontur, breite: float, tiefe: float):
    """2D-Bereich vor der Anlagekante: alles mit X <= F(y).

    Entsteht durch Ausschmieren der Kontur nach -X. Wie weit zurueck
    ausgeschmiert wird, ist fast egal -- der Streifen muss nur tiefer sein als
    der groesste Versatz, der spaeter davon abgezogen wird. Deshalb 'tiefe'
    statt einer festen Laenge: ungekuerzt kostet dieser Schritt ein Vielfaches.

    Aus dieser einen Flaeche entstehen alle Stirnflaechen des Bauteils. Wird
    sie senkrecht extrudiert, ergibt sie eine prismatische Anlageflaeche; wird
    sie schraeg extrudiert, die angestellte Rueckflaeche des Keils. Die
    Differenz zweier solcher Koerper ist der Keil selbst.
    """
    kontur = kontur & Rectangle(4000, breite + 8)
    if not kontur.faces():
        raise ValueError(
            "Die Kopfkontur liegt nicht vor der Stirnseite -- kopf_y pruefen. "
            "Der Knick sitzt seitlich neben dem Bauteil."
        )
    schritt = 0.3
    kopien = [Pos(-i * schritt, 0, 0) * kontur
              for i in range(1, int(tiefe / schritt) + 3)]
    return kontur.fuse(*kopien).clean()


def entschlacken(koerper, min_volumen: float, was: str):
    """Mikroskopische Splitter aus einem Booleschen Ergebnis entfernen.

    An der welligen Freiformkontur laesst OCCT beim Abziehen gern ein paar
    Dutzend Scherben von Tausendstel-Kubikmillimetern stehen. Sie sind weder
    druckbar noch gewollt, machen das Ergebnis formal aber zu einem Haufen
    einzelner Solids -- und damit zu einer unbrauchbaren STEP-Datei.

    Weggeworfen wird nur, was kleiner ist als ein Wuerfel mit der Kantenlaenge
    des Duesendurchmessers. Alles darueber bleibt stehen: bricht das Bauteil
    wirklich auseinander, soll die Pruefung das auch melden.

    Rueckgabe: (koerper, anzahl_entfernt).
    """
    solids = koerper.solids()
    if len(solids) <= 1:
        return koerper, 0
    gross = [s for s in solids if s.volume >= min_volumen]
    if len(gross) == len(solids):
        return koerper, 0
    if not gross:
        raise ValueError(f"{was}: es bleiben nur Splitter uebrig, kein Koerper.")
    neu = gross[0] if len(gross) == 1 else gross[0].fuse(*gross[1:])
    return neu, len(solids) - len(gross)


def stirn_werkzeug(region, z_von: float, z_bis: float, versatz: float,
                   winkel: float, ueberstand: float = 1.0):
    """Schnittwerkzeug fuer eine Stirnflaeche.

    Der Bereich vor der Anlagekante, ab z_von um 'versatz' zurueckgesetzt und
    mit 'winkel' nach hinten geneigt. Bei winkel = 0 entsteht eine senkrechte
    prismatische Flaeche, sonst eine Schraege.

    Der Ueberstand laeuft oben und unten aus dem Bauteil heraus, damit der
    Schnitt sauber austritt. Der Startversatz wird um die Neigung des
    Ueberstands korrigiert -- sonst saesse die Flaeche auf z_von nicht genau
    um 'versatz' zurueck.
    """
    a = math.radians(winkel)
    richtung = Vector(math.tan(a), 0, 1).normalized()
    hoehe = (z_bis - z_von) + 2 * ueberstand
    start_x = versatz - math.tan(a) * ueberstand
    return extrude(Pos(start_x, 0, z_von - ueberstand) * region,
                   amount=hoehe / math.cos(a), dir=richtung)


def klinge_bauen(region, p: ParameterSchneider):
    """Der einseitig angeschliffene Keil unter der Grundplatte.

    Vorderflaeche senkrecht auf der Anlagekante, Rueckflaeche um 'keilwinkel'
    angestellt, unten die Schneidfase. Beides sind Extrusionen derselben
    Stirnflaeche -- die eine senkrecht, die andere schraeg. Ihre Differenz ist
    genau der Keil:

        in der Hoehe h ueber der Schneide reicht das Material von
        F(y)  bis  F(y) + schneide_fase + tan(keilwinkel) * h

    Rueckgabe: (klinge, kennwerte).
    """
    z_unten = p.unterkante_bauteil
    z_oben = p.unterkante_z
    breite = p.klinge_breite if p.klinge_breite else p.breite

    hinten = stirn_werkzeug(region, z_unten, z_oben, p.schneide_fase, p.keilwinkel)
    vorne = stirn_werkzeug(region, z_unten, z_oben, 0.0, 0.0)
    klinge = hinten - vorne

    # Auf den tatsaechlichen Bauraum begrenzen: der Ueberstand der beiden
    # Werkzeuge ragt oben und unten heraus, und in Y ist die Stirnflaeche
    # breiter als das Bauteil.
    grenze = Pos(-1.0, 0, z_unten) * Box(
        p.laenge + 1.0, breite, p.klinge_hoehe,
        align=(Align.MIN, Align.CENTER, Align.MIN),
    )
    klinge = klinge & grenze
    if not klinge.solids():
        raise ValueError(
            "Der Keil ist leer. Pruefen: schneide_fase > 0, keilwinkel > 0, "
            "klinge_hoehe > 0."
        )
    klinge, splitter = entschlacken(klinge, p.duesen_d ** 3, "Keil")
    if splitter:
        print(f"      ({splitter} Splitter unter {p.duesen_d ** 3:.3f} mm3 "
              f"aus dem Keil entfernt)")

    return klinge, {
        "keil_dicke_schneide_mm": round(p.schneide_fase, 3),
        "keil_dicke_oben_mm": round(p.klingendicke(p.klinge_hoehe), 3),
        "keil_dicke_eintauch_mm": round(p.klingendicke(p.eintauchtiefe), 3),
        "keil_breite_mm": round(breite, 3),
        "keil_volumen_mm3": round(klinge.volume, 2),
    }


def automatik_aufloesen(p: ParameterBasis, formtiefe: float):
    """Alle "leer = automatisch"-Felder auf konkrete Zahlen setzen.

    Muss VOR dem ersten Geometrieschritt laufen: sowohl die Grundplatte als
    auch die Stirnleiste fragen diese Werte ab, und breite_bei() kann erst
    antworten, wenn Halsbreite und Schulterlage feststehen.
    """
    if not isinstance(p, ParameterHalter):
        return
    if p.anlage_tiefe is None:
        # Hinter dem tiefsten Punkt der Knickkontur muss noch Wand stehen,
        # sonst ist die Leiste dort hauchduenn. Drei Wandstaerken sind ein
        # brauchbarer Kompromiss zwischen Steifigkeit und Materialeinsatz.
        p.anlage_tiefe = round(formtiefe + 3 * p.min_wandstaerke, 3)
    if p.hals_ab_x is None:
        # Die volle Breite wird genau dort gebraucht, wo die Koepfe anliegen
        # -- also ueber die Tiefe der Stirnleiste. Dahinter darf es schmaler
        # werden, deshalb sitzt die Schulter direkt an deren Hinterkante.
        p.hals_ab_x = p.anlage_tiefe
    if p.hals_breite is None:
        # So schmal, wie Wellenbohrung, Senkung und Mutterntasche es zulassen.
        p.hals_breite = round(kopf_mindestbreite(p), 3)
    p.hals_breite = min(p.hals_breite, p.breite)


def anlage_tiefe_bestimmen(p: ParameterHalter, formtiefe: float) -> float:
    """Wie weit die Stirnleiste nach hinten reicht, wenn nichts vorgegeben ist.

    Hinter dem tiefsten Punkt der Knickkontur muss noch Wand stehen, sonst
    ist die Leiste dort hauchduenn. Drei Wandstaerken sind ein brauchbarer
    Kompromiss zwischen Steifigkeit und Materialeinsatz.
    """
    if p.anlage_tiefe is None:                 # falls jemand direkt aufruft
        automatik_aufloesen(p, formtiefe)
    return p.anlage_tiefe


def anlageblock_bauen(region, p: ParameterHalter, formtiefe: float):
    """Die Stirnleiste unter der Platte beim Halter.

    Nur die Stirnseite laeuft nach unten weiter, nicht die ganze Platte: alles
    hinter 'anlage_tiefe' faellt weg. Der Kopf liegt trotzdem ueber die volle
    Hoehe buendig an, denn die Knickkontur zieht sich senkrecht durch -- aber
    das Bauteil bleibt leicht und die Reihe kann darunter durchlaufen.

    Rueckgabe: (leiste, kennwerte).
    """
    z_unten = p.unterkante_bauteil
    tiefe = anlage_tiefe_bestimmen(p, formtiefe)
    if tiefe <= formtiefe:
        raise ValueError(
            f"anlage_tiefe ({tiefe} mm) reicht nicht hinter die Knickkontur "
            f"({round(formtiefe, 2)} mm tief) -- die Leiste haette Loecher."
        )
    leiste = Pos(0, 0, z_unten) * Box(
        tiefe, p.breite, p.anlage_hoehe,
        align=(Align.MIN, Align.CENTER, Align.MIN),
    )
    leiste = leiste - stirn_werkzeug(region, z_unten, p.unterkante_z, 0.0, 0.0)
    if not leiste.solids():
        raise ValueError("Die Stirnleiste ist leer -- anlage_tiefe oder kopf_y pruefen.")
    leiste, splitter = entschlacken(leiste, p.duesen_d ** 3, "Stirnleiste")
    if splitter:
        print(f"      ({splitter} Splitter aus der Stirnleiste entfernt)")
    return leiste, {
        "anlage_hoehe_mm": round(p.anlage_hoehe, 3),
        "anlage_tiefe_mm": round(tiefe, 3),
        "anlage_volumen_mm3": round(leiste.volume, 2),
        "hals_breite_mm": round(p.hals_breite, 3),
        "hals_ab_x_mm": round(p.hals_ab_x, 3),
    }


# ----------------------------------------------------------------------------
# 4. Grundplatte und Zusammenbau
# ----------------------------------------------------------------------------
def _y_zylinder(d: float, laenge: float, x: float, y_start: float, z: float):
    """Zylinder mit Achse parallel Y, ab y_start nach +Y.

    Cylinder() steht immer in Z; Rot(-90, 0, 0) dreht die lokale Z-Achse
    auf +Y. Alle Querbohrungen dieses Bauteils entstehen so.
    """
    return Pos(x, y_start, z) * Rot(-90, 0, 0) * Cylinder(
        d / 2, laenge, align=(Align.CENTER, Align.CENTER, Align.MIN)
    )


def _y_sechskant(sw: float, laenge: float, x: float, y_start: float, z: float,
                 drehung: float):
    """Sechskantprisma mit Achse parallel Y, ab y_start nach +Y.

    major_radius=False heisst: der Radius ist der Innenkreis, also SW/2 --
    genau die Schluesselweite, die die Mutter braucht. Nach Rot(-90, 0, 0)
    liegen die Schluesselflaechen bei drehung=0 oben und unten; das laesst
    ueber und unter der Tasche die dickste Restwand stehen.
    """
    profil = RegularPolygon(sw / 2, 6, major_radius=False, rotation=drehung)
    return Pos(x, y_start, z) * Rot(-90, 0, 0) * extrude(profil, amount=laenge)


def platte_bauen(p: ParameterBasis, region):
    """Grundplatte mit Stirnkontur -- noch ohne Bohrungen.

    Beim Halter laeuft die Kontur senkrecht durch (Ruecksprung und Freiwinkel
    sind null): der Kopf soll ja anliegen. Beim Schneider wird die Platte
    zurueckgesetzt und nach oben freigestellt, damit ausschliesslich der Keil
    Kontakt hat.
    """
    ruecksprung = getattr(p, "platte_ruecksprung", 0.0)
    freiwinkel = getattr(p, "platte_freiwinkel", 0.0)

    platte = Pos(0, 0, p.unterkante_z) * Box(
        p.laenge, p.breite, p.dicke,
        align=(Align.MIN, Align.CENTER, Align.MIN),
    )

    # --- Taille: hinter der Schulter auf den Hals einschnueren ---
    # Muss vor den Verrundungen passieren, damit die hinteren Ecken danach
    # auf der SCHMALEN Kante sitzen und nicht auf einer, die es nicht mehr gibt.
    hals = getattr(p, "hals_breite", None)
    ab_x = getattr(p, "hals_ab_x", None)
    if hals is not None and ab_x is not None and hals < p.breite - 1e-9:
        ueber = 1.0
        for vorzeichen in (+1, -1):
            platte = platte - Pos(ab_x, vorzeichen * (hals / 2), p.unterkante_z - ueber) * Box(
                p.laenge - ab_x + ueber, (p.breite - hals) / 2 + ueber,
                p.dicke + 2 * ueber,
                align=(Align.MIN, Align.MIN if vorzeichen > 0 else Align.MAX, Align.MIN),
            )
        # Schulter verrunden -- die Skizze zeigt einen weichen Uebergang,
        # und die Kerbe waere sonst die Sollbruchstelle des Bauteils.
        if p.hals_radius > 0:
            schulter = platte.edges().filter_by(Axis.Z).filter_by_position(
                Axis.X, ab_x - 1e-6, ab_x + 1e-6
            )
            if schulter:
                platte = fillet(schulter, p.hals_radius)

    # Nur die HINTEREN senkrechten Ecken runden. Vorne sitzt die Knickkontur;
    # ein Radius wuerde sie beschneiden.
    if p.eckradius > 0:
        hinten = platte.edges().filter_by(Axis.Z).filter_by_position(
            Axis.X, p.laenge - 1e-6, p.laenge + 1e-6
        )
        if hinten:
            platte = fillet(hinten, p.eckradius)

    # Oberkanten brechen, solange die Platte noch ein Quader ist. Nach dem
    # Konturschnitt geht das nicht mehr: OCCT legt den Radius auf der
    # Freiformkante nicht sauber an und laesst den Koerper um rund 0.06 mm
    # ueber sein Nennmass hinauslaufen. Die geformte Stirnkante bleibt damit
    # scharf -- was gewollt ist, denn genau dort liegt der Hairpin an.
    if p.kantenradius > 0:
        oben = platte.edges().filter_by_position(
            Axis.Z, p.oberkante_z - 1e-6, p.oberkante_z + 1e-6
        )
        if oben:
            platte = fillet(oben, p.kantenradius)

    platte = platte - stirn_werkzeug(region, p.unterkante_z, p.oberkante_z,
                                     ruecksprung, freiwinkel)
    if not platte.solids():
        raise ValueError(
            "Die Grundplatte ist leer -- die Kopfkontur schneidet sie ganz weg. "
            "laenge vergroessern oder kopf_y pruefen."
        )
    platte, splitter = entschlacken(platte, p.duesen_d ** 3, "Grundplatte")
    if splitter:
        print(f"      ({splitter} Splitter aus der Grundplatte entfernt)")
    return platte


def bauteil_bauen(p: ParameterBasis, region, formtiefe: float):
    """Platte + Unterbau, danach Bohrungen, Schlitz, Senkung und Mutterntasche.

    Rueckgabe: (koerper, kennwerte). In den Kennwerten steht, wieviel Material
    jedes einzelne Feature abgetragen hat -- Grundlage der Pruefung.
    """
    kw: dict = {}
    ueberstand = 1.0

    automatik_aufloesen(p, formtiefe)
    platte = platte_bauen(p, region)

    if isinstance(p, ParameterSchneider):
        unterbau, unterbau_kw = klinge_bauen(region, p)
    else:
        unterbau, unterbau_kw = anlageblock_bauen(region, p, formtiefe)
    kw.update(unterbau_kw)

    koerper = platte + unterbau
    kw["rohteil_volumen_mm3"] = round(koerper.volume, 2)

    def abziehen(name: str, werkzeug):
        nonlocal koerper
        vorher = koerper.volume
        koerper = koerper - werkzeug
        kw[f"abtrag_{name}_mm3"] = round(vorher - koerper.volume, 2)

    # --- Hauptbohrung, Achse Z, durch die Platte ---
    abziehen("hauptbohrung", Pos(p.bohrung_x, 0, p.unterkante_z - ueberstand) * Cylinder(
        p.bohrung_d / 2, p.dicke + 2 * ueberstand,
        align=(Align.CENTER, Align.CENTER, Align.MIN),
    ))

    # --- Klemmschlitz: beginnt in der Bohrungsachse (so haengt er immer an der
    #     Bohrung, egal wie gross die ist) und laeuft bis hinten raus ---
    abziehen("klemmschlitz", Pos(p.bohrung_x, 0, p.unterkante_z - ueberstand) * Box(
        p.laenge - p.bohrung_x + ueberstand, p.schlitz_breite, p.dicke + 2 * ueberstand,
        align=(Align.MIN, Align.CENTER, Align.MIN),
    ))

    # --- Klemmschraube quer durch den Schlitz, Achse Y, durchgehend ---
    abziehen("schraube", _y_zylinder(
        p.schraube_d, p.klemm_breite + 2 * ueberstand,
        p.schraube_x, p.aussen_y - ueberstand, p.mitte_z,
    ))

    # --- Senkung fuer den Schraubenkopf, nur von der -Y-Seite ---
    abziehen("senkung", _y_zylinder(
        p.senkung_d, p.senkung_tiefe + ueberstand,
        p.schraube_x, p.aussen_y - ueberstand, p.mitte_z,
    ))

    # --- Sechskanttasche fuer die Mutter, von der +Y-Seite herein ---
    mutter = _y_sechskant(
        p.mutter_sw_gesamt, p.mutter_tiefe + ueberstand, p.schraube_x,
        p.klemm_breite / 2 - p.mutter_tiefe, p.mitte_z, p.mutter_drehung,
    )
    # Hoehe der Tasche ueber Z messen statt rechnen -- haengt an mutter_drehung
    kw["mutter_hoehe_z_mm"] = round(mutter.bounding_box().size.Z, 3)
    abziehen("mutter", mutter)

    bb = koerper.bounding_box()
    kw["gesamt_volumen_mm3"] = round(koerper.volume, 2)
    kw["gesamt_solids"] = len(koerper.solids())
    kw["bauraum"] = [round(bb.size.X, 3), round(bb.size.Y, 3), round(bb.size.Z, 3)]
    return koerper, kw


# ----------------------------------------------------------------------------
# 5. Pruefung des erzeugten Koerpers
# ----------------------------------------------------------------------------
def pruefen(koerper, kw: dict, p: ParameterBasis) -> list[dict]:
    """Geometrie- und Fertigungspruefung.

    Geprueft wird gegen die Parameter (stimmt das Ergebnis mit der Vorgabe
    ueberein?) und gegen die Fertigungsgrenzen des FDM-Drucks (bleibt ueberall
    genug Wand stehen, ist die Schneide ueberhaupt druckbar?).
    """
    checks: list[dict] = []

    def add(name, wert, soll, tol, einheit=""):
        checks.append({
            "kriterium": name, "wert": round(wert, 3), "soll": round(soll, 3),
            "toleranz": tol, "einheit": einheit,
            "status": "OK" if abs(wert - soll) <= tol else "ABWEICHUNG",
        })

    def mindestens(name, wert, grenze, einheit=""):
        # Auf 3 Stellen gerundet vergleichen -- sonst meldet z.B. 9.2/2 - 3.0
        # = 1.5999999999999996 eine Abweichung, obwohl die Tabelle "1.6 gegen
        # Soll 1.6" anzeigt. Der Vergleich muss zu dem passen, was dasteht.
        wert, grenze = round(wert, 3), round(grenze, 3)
        checks.append({
            "kriterium": name, "wert": wert, "soll": grenze,
            "toleranz": "min", "einheit": einheit,
            "status": "OK" if wert >= grenze else "ABWEICHUNG",
        })

    bb = koerper.bounding_box()
    gesamthoehe = p.dicke + p.unterbau_hoehe

    # a) Ergebnis stimmt mit der Vorgabe ueberein
    add("Ergebnis ist EIN Solid", len(koerper.solids()), 1, 0, "")
    # X = 0 ist der vorstehendste Punkt der Anlagekante, hinten endet das
    # Bauteil bei 'laenge'. Die Toleranz deckt ab, dass dieser Punkt nur an
    # kopf_abtastung Stuetzstellen gesucht wird -- dazwischen kann die stetige
    # Kontur noch ein paar Hundertstel weiter vorstehen.
    add("Bauraum X", bb.size.X, p.laenge, 0.05, "mm")
    add("Bauraum Y", bb.size.Y, p.breite, 0.01, "mm")
    add("Bauraum Z", bb.size.Z, gesamthoehe, 0.01, "mm")
    add("Unterkante Z", bb.min.Z, p.unterkante_bauteil, 0.01, "mm")

    # b) jedes Feature hat wirklich Material abgetragen
    for feature in ["hauptbohrung", "klemmschlitz", "schraube", "senkung", "mutter"]:
        mindestens(f"Abtrag {feature}", kw[f"abtrag_{feature}_mm3"], 0.01, "mm3")

    # c) Restwaende in der Plattenebene
    mindestens("Wand Hauptbohrung <-> Stirnkante",
               p.bohrung_x - p.bohrung_d / 2 - kw["stirn_formtiefe_mm"],
               p.min_wandstaerke, "mm")
    mindestens("Wand Hauptbohrung <-> Laengsseite",
               p.breite_bei(p.bohrung_x) / 2 - p.bohrung_d / 2,
               p.min_wandstaerke, "mm")
    mindestens("Wand Senkung <-> Hinterkante",
               p.laenge - p.schraube_x - p.senkung_d / 2, p.min_wandstaerke, "mm")

    # d) Restwaende ueber der Dicke -- hier entscheidet sich, ob die Platte
    #    dick genug ist. Die Senkung ist der kritische Fall, sie ist breiter
    #    als die Schraube.
    mindestens("Wand ueber Klemmschraube",
               p.dicke / 2 - p.schraube_d / 2, p.min_wandstaerke, "mm")
    mindestens("Wand ueber Senkung",
               p.dicke / 2 - p.senkung_d / 2, p.min_wandstaerke, "mm")
    mindestens("Wand ueber Mutterntasche",
               p.dicke / 2 - kw["mutter_hoehe_z_mm"] / 2, p.min_wandstaerke, "mm")

    # e) der Klemmbereich muss federn koennen: die Schraube muss hinter der
    #    Bohrung sitzen, sonst kreuzt sie den Schlitz nicht
    mindestens("Schraube sitzt hinter der Hauptbohrung",
               p.schraube_x - (p.bohrung_x + p.bohrung_d / 2), 0.0, "mm")

    # f) Senkungsgrund muss vor dem Schlitz enden, sonst faellt der
    #    Schraubenkopf durch
    mindestens("Material Senkungsgrund <-> Schlitz",
               (p.klemm_breite / 2 - p.senkung_tiefe) - p.schlitz_breite / 2,
               p.min_wandstaerke, "mm")

    # g) die Mutter muss ueber die Schraube passen und darf den Schlitz nicht
    #    erreichen, sonst hat sie keine Auflage
    mindestens("Mutter passt ueber die Schraube",
               p.mutter_sw_gesamt - p.schraube_d, 0.0, "mm")
    mindestens("Material Mutterngrund <-> Schlitz",
               (p.klemm_breite / 2 - p.mutter_tiefe) - p.schlitz_breite / 2,
               p.min_wandstaerke, "mm")

    # h) die Knickform muss ueber die ganze Breite ankommen, sonst liegt der
    #    Hairpin nur punktuell an
    add("Anlagekante ueber die volle Breite",
        kw["stirn_stuetzstellen"], p.kopf_abtastung, 0, "")

    # i) variantenspezifisch
    if isinstance(p, ParameterSchneider):
        # Die Schneide ist eine Kante, kein Messer: unter einer halben
        # Duesenbreite kann der Drucker sie nicht mehr auftragen.
        mindestens("Schneidfase druckbar", p.schneide_fase, p.duesen_d / 2, "mm")
        # Der Keil soll hoeher sein als der Arbeitshub -- der Rest ist Puffer.
        mindestens("Puffer ueber der Eintauchtiefe",
                   p.klinge_hoehe - p.eintauchtiefe, 0.0, "mm")
        # Am oberen Ende darf der Keil die Platte nicht ueberragen, sonst
        # traegt nicht mehr die Platte, sondern die Schraege.
        mindestens("Platte steht hinter der Keiloberkante",
                   p.laenge - kw["stirn_formtiefe_mm"] - kw["keil_dicke_oben_mm"],
                   p.bohrung_x + p.bohrung_d / 2 + p.min_wandstaerke, "mm")
        # Die Platte darf den Hairpin nicht beruehren.
        mindestens("Platte freigestellt gegen den Hairpin",
                   p.platte_ruecksprung, 0.0, "mm")
    else:
        # Beim Halter traegt die Stirnflaeche -- sie muss ueberhaupt da sein.
        mindestens("Stirnleiste vorhanden", kw["anlage_volumen_mm3"], 0.01, "mm3")
        # Hinter dem tiefsten Punkt der Knickkontur muss Wand stehen bleiben,
        # sonst ist die Leiste dort papierduenn.
        mindestens("Wand hinter der Knickkontur",
                   kw["anlage_tiefe_mm"] - kw["stirn_formtiefe_mm"],
                   p.min_wandstaerke, "mm")
        # Die Leiste darf nicht bis unter die Wellenbohrung reichen -- dort
        # soll die Reihe unter der Platte durchlaufen koennen.
        mindestens("Stirnleiste endet vor der Wellenbohrung",
                   (p.bohrung_x - p.bohrung_d / 2) - kw["anlage_tiefe_mm"],
                   0.0, "mm")
        # Die Taille darf die Klemmung nicht erdruecken.
        mindestens("Hals breit genug fuer die Klemmung",
                   p.hals_breite, kopf_mindestbreite(p), "mm")
        # Die Schulter muss hinter der Anlageflaeche sitzen, sonst wird
        # ausgerechnet dort eingeschnuert, wo die Koepfe tragen.
        mindestens("Schulter hinter der Stirnleiste",
                   p.hals_ab_x - kw["anlage_tiefe_mm"], 0.0, "mm")
        # Die Schulterverrundung muss in die Stufe passen -- in der Tiefe
        # wie in der Laenge. Sonst kann OCCT sie nicht anlegen. Wie nah sie
        # der Wellenbohrung kommt, deckt "Wand Hauptbohrung <-> Laengsseite"
        # ab: die Schulter sitzt seitlich am Hals, nicht vor der Bohrung.
        mindestens("Schulterradius passt in die Stufe",
                   (p.breite - p.hals_breite) / 2, p.hals_radius, "mm")
        mindestens("Schulterradius passt in die Restlaenge",
                   p.laenge - p.hals_ab_x, p.hals_radius, "mm")

    return checks


# ----------------------------------------------------------------------------
# 6. Ausgabe
# ----------------------------------------------------------------------------
def exportieren(koerper, kw, checks, p: ParameterBasis, stem: str, outdir: Path):
    outdir.mkdir(parents=True, exist_ok=True)

    export_step(koerper, str(outdir / f"{stem}.step"))
    export_stl(koerper, str(outdir / f"{stem}.stl"))

    # Schnitte fuer die Dokumentation. ExportSVG zeichnet nur, was in der
    # XY-Ebene liegt -- to_local_coords() dreht den Schnitt jeweils dorthin.
    #
    # Die beiden naheliegenden Ebenen (halbe Hoehe, Y = 0) sind unbrauchbar:
    # sie liegen genau auf der Schraubenachse bzw. mitten im Klemmschlitz und
    # zerlegen das Bauteil optisch in drei Teile. Deshalb versetzt geschnitten.
    schnitte = {
        # knapp unter der Oberseite: Umriss + Hauptbohrung + Schlitz
        "draufsicht":    Plane.XY.offset(p.oberkante_z - 0.5),
        # neben dem Schlitz, aber noch innerhalb der Hauptbohrung: zeigt beide
        # Bohrungen UND das Keilprofil ueber die Hoehe
        "laengsschnitt": Plane(origin=(0, -p.bohrung_d / 4, 0), z_dir=(0, 1, 0)),
        # durch die Schraubenachse: zeigt Senkung und Schlitz ueber die Breite
        "querschnitt":   Plane(origin=(p.schraube_x, 0, 0), z_dir=(1, 0, 0)),
        # knapp ueber der Unterkante: die Schneide bzw. die Anlagekante
        "schneide":      Plane.XY.offset(p.unterkante_bauteil + 0.3),
    }
    for name, ebene in schnitte.items():
        try:
            svg = ExportSVG()
            svg.add_shape(ebene.to_local_coords(section(koerper, section_by=ebene)))
            svg.write(str(outdir / f"{stem}_{name}.svg"))
        except Exception as e:
            print(f"  (SVG {name} uebersprungen: {e})")

    report = {"variante": type(p).__name__, "parameter": asdict(p),
              "kennwerte": kw, "pruefung": checks}
    (outdir / f"{stem}_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8"
    )

    zeilen = [f"# Pruefprotokoll {stem}", "", "## Kennwerte"]
    zeilen += [f"- {k}: {v}" for k, v in kw.items()]
    zeilen += ["", "## Pruefung des erzeugten Koerpers", "",
               "| Kriterium | Wert | Soll | Toleranz | Status |", "|---|---|---|---|---|"]
    for c in checks:
        zeilen.append(
            f"| {c['kriterium']} | {c['wert']} {c['einheit']} | "
            f"{c['soll']} {c['einheit']} | {c['toleranz']} | {c['status']} |"
        )
    (outdir / f"{stem}_report.md").write_text("\n".join(zeilen), encoding="utf-8")


# ----------------------------------------------------------------------------
# 7. Ablauf
# ----------------------------------------------------------------------------
def stirnform_vorbereiten(pin, p: ParameterBasis):
    """Vom Hairpin zur fertig platzierten Stirnflaeche.

    Reihenfolge ist wichtig: erst die Biegung finden und die Breite daraus
    setzen, dann die Kontur so verschieben, dass die Biegungsmitte auf Y = 0
    liegt, und erst danach die Anlagekante ueber die endgueltige Breite
    abtasten. Zuletzt wandert die Kontur so weit nach hinten, dass ihr
    vorstehendster Punkt auf X = 0 liegt -- das ist der Nullpunkt, auf den
    sich alle Laengsmasse beziehen.

    Rueckgabe: (region, kennwerte).
    """
    kontur, kw = kopf_kontur(pin, p)
    print(f"      Kopf        : liegt {kw['kopf_liegt']}, "
          f"Spannweite {kw['kopf_spannweite_mm']} mm, "
          f"{kw['kopf_schnitte']} Projektionsschnitte")

    if p.kopf_breite_ableiten:
        # Die Biegung bestimmt, wie breit das Bauteil sein muss: ihre beiden
        # Endpunkte sollen genau auf den Ecken der Stirnseite liegen.
        mitte_y, biegung_breite, masse = kopf_biegung(kontur, p)
        kw.update(masse)
        kontur = Pos(0, -mitte_y, 0) * kontur
        print(f"      Biegung     : Kern {masse['kopf_kern_von_mm']} .. "
              f"{masse['kopf_kern_bis_mm']} mm, mit Auslauf "
              f"{masse['kopf_biegung_breite_mm']} mm breit")

        noetig = kopf_mindestbreite(p)
        # Der Halter soll die Koepfe abstuetzen, nicht nur die Biegung zeigen
        # -- er streckt die Breite ueber breite_faktor. Beim Schneider ist der
        # Faktor 1, dort zaehlt nur die Biegung selbst.
        gewuenscht = biegung_breite * p.breite_faktor
        breite = max(gewuenscht, noetig)
        # Breiter als die Kontur reicht darf das Bauteil nicht werden: dort
        # gaebe es nichts zu schneiden, die Stirnseite bliebe stehen und der
        # Koerper zerfiele in mehrere Stuecke. Nach dem Zentrieren ist die
        # nutzbare Breite der symmetrische Teil der Kontur um Y = 0.
        kb = kontur.bounding_box()
        vorhanden = 2 * min(abs(kb.min.Y), abs(kb.max.Y))
        if breite > vorhanden:
            print(f"      Breite      : auf {round(vorhanden, 3)} mm begrenzt -- "
                  f"so weit reicht die Kopfkontur ueberhaupt")
            breite = vorhanden
        if breite < noetig - 1e-6:
            raise ValueError(
                f"Die Kopfkontur ist nur {round(breite, 2)} mm breit, der "
                f"Klemmbereich braucht aber {round(noetig, 2)} mm. Entweder ist "
                f"die STEP-Datei kein einzelner Hairpin, oder Bohrung, Senkung "
                f"und Mutterntasche muessen kleiner werden."
            )
        kw["kopf_breite_gesetzt_mm"] = round(breite, 3)
        if p.breite_faktor != 1.0:
            print(f"      Breite      : {round(breite, 3)} mm -- Biegung "
                  f"{round(biegung_breite, 3)} mm x Faktor {p.breite_faktor}")
        elif breite > biegung_breite + 1e-9:
            print(f"      Breite      : {round(breite, 3)} mm -- auf das Machbare "
                  f"aufgeweitet (Biegung braeuchte nur {round(biegung_breite, 3)})")
        else:
            print(f"      Breite      : {round(breite, 3)} mm -- aus der Biegung")
        p.breite = breite

    kontur = Pos(0, p.kopf_y, 0) * kontur

    kante = kopf_anlagekante(kontur, p.breite, p.kopf_abtastung)
    if len(kante) < p.kopf_abtastung:
        # Ohne durchgehende Anlagekante laesst sich die Stirnseite nicht
        # formen: wo die Kontur fehlt, bleibt der Rohquader stehen und das
        # Ergebnis zerfaellt. Lieber hier abbrechen als ein kaputtes STEP
        # ausliefern.
        raise ValueError(
            f"Die Kopfkontur deckt die Stirnseite nur an {len(kante)} von "
            f"{p.kopf_abtastung} Stellen ab. Die Kontur ist schmaler als das "
            f"Bauteil oder sitzt seitlich daneben -- kopf_y pruefen, breite "
            f"verkleinern, oder kopf_breite_ableiten eingeschaltet lassen."
        )
    xs = [x for _, x in kante]
    # Vorstehendsten Punkt auf X = 0 legen. Danach ist die Anlagekante
    # ueberall >= 0 und das Bauteil liegt sauber in X = 0..laenge.
    kontur = Pos(-min(xs), 0, 0) * kontur
    formtiefe = max(xs) - min(xs)
    kw["stirn_formtiefe_mm"] = round(formtiefe, 3)
    kw["stirn_stuetzstellen"] = len(kante)
    print(f"      Knickform   : {round(formtiefe, 3)} mm tief ueber "
          f"{round(p.breite, 3)} mm Breite, {len(kante)}/{p.kopf_abtastung} Stuetzstellen")

    # Der Streifen muss tiefer sein als der groesste Versatz, der davon
    # abgezogen wird: Keildicke oben bzw. Ruecksprung samt Freiwinkel.
    if isinstance(p, ParameterSchneider):
        noetig = max(p.klingendicke(p.klinge_hoehe),
                     p.platte_ruecksprung
                     + math.tan(math.radians(p.platte_freiwinkel)) * p.dicke)
    else:
        noetig = 0.0
    region = stirn_region(kontur, p.breite, noetig + 3.0)

    return region, kw


def lauf(p: ParameterBasis, outdir: Path, stem: str = "endeffektor",
         hairpin: Path | None = None):
    """Kompletter Durchlauf. Rueckgabe: (koerper, kennwerte, checks).

    Ohne Hairpin geht nichts: die ganze Stirnform kommt aus der STEP-Datei.
    """
    if hairpin is None:
        raise ValueError(
            "Fuer beide Endeffektoren wird ein Hairpin-STEP gebraucht -- "
            "die Stirnform wird daraus abgeleitet."
        )
    art = "Schneider (Keil)" if isinstance(p, ParameterSchneider) else "Halter (Anlage)"

    print(f"[1/4] Hairpin     : {hairpin}")
    print(f"      Variante    : {art}")
    pin = import_step(str(hairpin))
    region, kopf_kw = stirnform_vorbereiten(pin, p)

    print(f"[2/4] Geometrie   : {round(p.laenge, 2)} x {round(p.breite, 2)} x "
          f"{round(p.dicke + p.unterbau_hoehe, 2)} mm, "
          f"Z = {round(p.unterkante_bauteil, 2)}..{round(p.oberkante_z, 2)}")
    koerper, kw = bauteil_bauen(p, region, kopf_kw["stirn_formtiefe_mm"])
    kw.update(kopf_kw)
    if isinstance(p, ParameterSchneider):
        print(f"      Keil        : {p.klinge_hoehe} mm hoch, {p.keilwinkel} Grad, "
              f"Schneide {kw['keil_dicke_schneide_mm']} mm")
        print(f"      Bei {p.eintauchtiefe} mm Eintauchtiefe: "
              f"{kw['keil_dicke_eintauch_mm']} mm dick "
              f"(oben {kw['keil_dicke_oben_mm']} mm)")
    else:
        print(f"      Stirnleiste : {p.anlage_hoehe} mm hoch, "
              f"{kw['anlage_tiefe_mm']} mm tief, Kontur senkrecht durch")
        print(f"      Taille      : ab X = {kw['hals_ab_x_mm']} mm auf "
              f"{kw['hals_breite_mm']} mm Hals (Stirnseite {round(p.breite, 2)} mm), "
              f"Schulterradius {p.hals_radius} mm")
    print(f"      Rohteil     : {kw['rohteil_volumen_mm3']} mm3")
    for feature in ["hauptbohrung", "klemmschlitz", "schraube", "senkung", "mutter"]:
        print(f"      - {feature:<13}: {kw[f'abtrag_{feature}_mm3']:>8} mm3 abgetragen")
    print(f"      Ergebnis    : {kw['gesamt_volumen_mm3']} mm3")

    print("[3/4] Pruefung    :")
    checks = pruefen(koerper, kw, p)
    for c in checks:
        flag = "  ok " if c["status"] == "OK" else " !!! "
        print(f"     {flag} {c['kriterium']:<42} {c['wert']:>10} {c['einheit']:<4}"
              f" (Soll {c['soll']})")

    print(f"[4/4] Export      : {outdir}/{stem}.step | .stl | _report.md")
    exportieren(koerper, kw, checks, p, stem, outdir)

    if any(c["status"] != "OK" for c in checks):
        print("\nACHTUNG: mindestens ein Kriterium weicht ab -- Parameter pruefen.")
    return koerper, kw, checks


def main():
    ap = argparse.ArgumentParser(
        description="Generator fuer die beiden Hairpin-Endeffektoren")
    ap.add_argument("--variante", choices=sorted(VARIANTEN), default="schneider",
                    help="halter = Buendel aufhalten, schneider = vereinzeln")
    ap.add_argument("--hairpin", default="PEM-Referenzstator-Aussen.STEP",
                    help="STEP-Datei des Hairpins; liefert die Stirnform")
    ap.add_argument("--dicke", type=float, help="Plattendicke [mm]")
    ap.add_argument("--breite", type=float,
                    help="Breite Y [mm]; schaltet die Ableitung aus der Biegung ab")
    ap.add_argument("--eckradius", type=float, help="Radius der hinteren Ecken [mm]")
    ap.add_argument("--kantenradius", type=float, help="Bruch der Oberkanten [mm]")
    ap.add_argument("--kopf-spiel", type=float, dest="kopf_spiel",
                    help="Abstand Stirnflaeche <-> Hairpin [mm]")
    ap.add_argument("--kopf-y", type=float, dest="kopf_y",
                    help="Kontur quer verschieben [mm]")
    ap.add_argument("--breite-faktor", type=float, dest="breite_faktor",
                    help="Stirnseite so viel breiter als die Biegung")
    # nur Schneider
    ap.add_argument("--klinge-hoehe", type=float, dest="klinge_hoehe",
                    help="gebaute Hoehe des Keils [mm]")
    ap.add_argument("--eintauchtiefe", type=float, dest="eintauchtiefe",
                    help="Arbeitshub des Keils [mm]")
    ap.add_argument("--keilwinkel", type=float, dest="keilwinkel",
                    help="Anstellung der Keilrueckflaeche [Grad]")
    ap.add_argument("--schneide-fase", type=float, dest="schneide_fase",
                    help="Dicke der Schneidkante [mm]")
    ap.add_argument("--platte-freiwinkel", type=float, dest="platte_freiwinkel",
                    help="Ruecknahme der Plattenstirnflaeche nach oben [Grad]")
    # nur Halter
    ap.add_argument("--anlage-hoehe", type=float, dest="anlage_hoehe",
                    help="Hoehe der Stirnleiste [mm]")
    ap.add_argument("--anlage-tiefe", type=float, dest="anlage_tiefe",
                    help="wie weit die Stirnleiste nach hinten reicht [mm]")
    ap.add_argument("--out", default=None, help="Ausgabeordner")
    a = ap.parse_args()

    p = VARIANTEN[a.variante]()
    gueltig = {f.name for f in fields(p)}
    for name in ("dicke", "breite", "eckradius", "kantenradius", "kopf_spiel",
                 "kopf_y", "breite_faktor", "klinge_hoehe", "eintauchtiefe",
                 "keilwinkel", "schneide_fase", "platte_freiwinkel",
                 "anlage_hoehe", "anlage_tiefe"):
        wert = getattr(a, name, None)
        if wert is None:
            continue
        if name not in gueltig:
            raise SystemExit(f"--{name.replace('_', '-')} gibt es bei der "
                             f"Variante '{a.variante}' nicht.")
        setattr(p, name, wert)
    # Eine von Hand gesetzte Breite soll nicht gleich wieder ueberschrieben werden.
    if a.breite is not None:
        p.kopf_breite_ableiten = False

    hairpin = Path(a.hairpin).expanduser()
    if not hairpin.is_absolute():
        hairpin = (Path.cwd() / hairpin) if (Path.cwd() / hairpin).exists() else HIER / hairpin
    if not hairpin.exists():
        raise SystemExit(f"Hairpin-STEP nicht gefunden: {hairpin}")

    outdir = Path(a.out).expanduser() if a.out else HIER / "out"
    lauf(p, outdir, stem=f"endeffektor_{a.variante}", hairpin=hairpin)


if __name__ == "__main__":
    main()
