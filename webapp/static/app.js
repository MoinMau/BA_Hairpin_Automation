/*
 * app.js -- Ablaufsteuerung der Oberflaeche.
 *
 * Das Parameterformular wird nicht von Hand gepflegt, sondern aus
 * /api/schema erzeugt -- und das kommt direkt aus den dataclasses der
 * Generatoren. Ein neuer Parameter im Python-Code erscheint dadurch
 * automatisch im Browser.
 */

import { Viewer, parseSTL } from "/static/viewer.js";

const $ = (id) => document.getElementById(id);

const zustand = {
  bauteile: [],
  schema: null,
  hairpins: [],
  ergebnis: null,      // letzte Antwort von /api/generieren
  anzeige: null,       // "bauteil" | "hairpin"
};

const FARBE = {
  bauteil: [0.42, 0.68, 0.95],
  hairpin: [0.85, 0.62, 0.28],
};

let viewer;
try {
  viewer = new Viewer($("viewer"));
} catch (e) {
  meldung("fehler", e.message);
}


// ---------------------------------------------------------------- Netzwerk
async function hole(pfad, optionen) {
  const antwort = await fetch(pfad, optionen);
  const typ = antwort.headers.get("Content-Type") || "";
  if (!antwort.ok) {
    let text = `HTTP ${antwort.status}`;
    if (typ.includes("json")) {
      try { text = (await antwort.json()).fehler || text; } catch { /* egal */ }
    }
    throw new Error(text);
  }
  return typ.includes("json") ? antwort.json() : antwort.arrayBuffer();
}


// ---------------------------------------------------------------- Meldungen
function meldung(art, text) {
  $("meldung").innerHTML = text
    ? `<div class="banner ${art}"><span>${art === "ok" ? "&#10003;" : "&#9888;"}</span><span>${esc(text)}</span></div>`
    : "";
}
const esc = (s) => String(s).replace(/[&<>"]/g, (c) =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

function laden(an, text) {
  $("lade").classList.toggle("an", an);
  if (text) $("lade-text").textContent = text;
}


// ---------------------------------------------------------------- Bauteile
async function bauteileLaden() {
  zustand.bauteile = await hole("/api/bauteile");
  $("bauteil").innerHTML = zustand.bauteile
    .map((b) => `<option value="${b.id}">${esc(b.titel)}</option>`).join("");
  await schemaLaden();
}

async function schemaLaden() {
  const id = $("bauteil").value;
  zustand.schema = await hole(`/api/schema?bauteil=${encodeURIComponent(id)}`);
  $("bauteil-beschreibung").textContent = zustand.schema.beschreibung;
  $("karte-hairpin").classList.toggle("versteckt", zustand.schema.hairpin === "nein");
  $("hairpin-hinweis").textContent = zustand.schema.hairpin === "optional"
    ? "Optional – ohne Hairpin entsteht das Bauteil ohne Kopfaufnahme."
    : "Geometrie, aus der die Wirkkontur abgeleitet wird.";
  formularBauen(zustand.schema);
  await hairpinsLaden();
  ergebnisLeeren();
}

/** Formular aus der Schemabeschreibung aufbauen. */
function formularBauen(schema) {
  $("parameter-gruppen").innerHTML = schema.gruppen.map((g, i) => `
    <details class="gruppe" ${i === 0 || i === 2 ? "open" : ""}>
      <summary>${esc(g.titel)}</summary>
      <div class="felder">${g.felder.map(feldHtml).join("")}</div>
    </details>`).join("");
}

function feldHtml(f) {
  const wert = f.standard === null || f.standard === undefined ? "" : f.standard;
  const eingabe = f.auswahl
    ? `<select id="p-${f.name}" data-param="${f.name}">${
        f.auswahl.map((o) => `<option ${o === wert ? "selected" : ""}>${esc(o)}</option>`).join("")}</select>`
    : `<input type="${f.typ === "text" ? "text" : "number"}" step="${f.typ === "ganzzahl" ? "1" : "any"}"
              id="p-${f.name}" data-param="${f.name}" value="${esc(wert)}"
              ${f.optional ? 'placeholder="automatisch"' : ""}>`;
  return `<div class="feld">
    <label for="p-${f.name}">${esc(f.label)}</label>
    <div class="zeile">${eingabe}<span class="einheit">${esc(f.einheit || "")}</span></div>
  </div>`;
}

function parameterLesen() {
  const werte = {};
  document.querySelectorAll("[data-param]").forEach((el) => { werte[el.dataset.param] = el.value; });
  return werte;
}


// ---------------------------------------------------------------- Hairpins
async function hairpinsLaden(auswaehlen) {
  zustand.hairpins = await hole("/api/hairpins");
  const vorher = auswaehlen || $("hairpin").value;
  const optional = zustand.schema?.hairpin === "optional";
  const eintraege = zustand.hairpins.map((h) =>
    `<option value="${esc(h.id)}">${esc(h.name)} &nbsp;·&nbsp; ${h.groesse_kb} kB · ${esc(h.quelle)}</option>`);
  if (optional) eintraege.unshift(`<option value="">— ohne Kopfaufnahme —</option>`);
  $("hairpin").innerHTML = eintraege.length
    ? eintraege.join("")
    : `<option value="">— keine STEP-Datei gefunden —</option>`;
  if (vorher && zustand.hairpins.some((h) => h.id === vorher)) $("hairpin").value = vorher;
}

async function hochladen(datei) {
  if (!/\.(step|stp)$/i.test(datei.name)) {
    return meldung("fehler", "Nur .step- oder .stp-Dateien werden angenommen.");
  }
  laden(true, `${datei.name} wird hochgeladen …`);
  try {
    const antwort = await hole("/api/upload", {
      method: "POST",
      headers: { "X-Dateiname": encodeURIComponent(datei.name), "Content-Type": "application/octet-stream" },
      body: datei,
    });
    await hairpinsLaden(antwort.id);
    meldung("ok", `${datei.name} hochgeladen und ausgewählt.`);
  } catch (e) {
    meldung("fehler", `Upload fehlgeschlagen: ${e.message}`);
  } finally {
    laden(false);
  }
}


// ---------------------------------------------------------------- Generieren
async function generieren() {
  const bauteil = $("bauteil").value;
  const hairpin = $("hairpin").value;
  if (zustand.schema.hairpin === "pflicht" && !hairpin) {
    return meldung("fehler", "Bitte zuerst eine Hairpin-STEP-Datei auswählen oder hochladen.");
  }

  meldung("", "");
  laden(true, "Geometrie wird berechnet …");
  $("btn-generieren").disabled = true;
  try {
    const e = await hole("/api/generieren", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ bauteil, hairpin, parameter: parameterLesen() }),
    });
    zustand.ergebnis = e;
    ergebnisAnzeigen(e);
    await meshZeigen("bauteil");
    meldung(e.alles_ok ? "ok" : "warn", e.alles_ok
      ? `${e.titel} erzeugt – alle ${e.pruefung.length} Prüfkriterien erfüllt.`
      : `${e.titel} erzeugt, aber ${e.pruefung.filter((c) => c.status !== "OK").length} Kriterium/Kriterien verletzt – siehe Prüfprotokoll.`);
  } catch (err) {
    meldung("fehler", `Generierung fehlgeschlagen: ${err.message}`);
  } finally {
    laden(false);
    $("btn-generieren").disabled = false;
  }
}


// ---------------------------------------------------------------- Anzeige
function ergebnisAnzeigen(e) {
  // --- Downloads: STEP zuerst, das ist das Arbeitsergebnis ---
  const beschriftung = {
    step: ["STEP", "CAD-Modell für Fusion, SolidWorks, FreeCAD …"],
    stl: ["STL", "Netz für den 3D-Druck / Slicer"],
    md: ["MD", "Prüfprotokoll zum Lesen"],
    json: ["JSON", "Prüfprotokoll maschinenlesbar"],
    svg: ["SVG", "Schnittzeichnung"],
  };
  const rang = { step: 0, stl: 1, md: 2, json: 3, svg: 4 };
  const dateien = [...e.dateien].sort((a, b) =>
    (rang[endung(a)] ?? 9) - (rang[endung(b)] ?? 9) || a.localeCompare(b));

  $("download-liste").innerHTML = dateien.map((name, i) => {
    const [typ, was] = beschriftung[endung(name)] || [endung(name).toUpperCase(), ""];
    return `<a class="download ${i === 0 ? "haupt" : ""}"
               href="/api/datei/${encodeURIComponent(e.job)}/${encodeURIComponent(name)}?download=1"
               download="${esc(name)}">
        <span class="typ">${esc(typ)}</span>
        <span class="name">${esc(name)}<br><span style="color:var(--text-schwach);font-size:11px">${esc(was)}</span></span>
        <span class="pfeil">&#8595;</span>
      </a>`;
  }).join("");
  $("karte-download").classList.remove("versteckt");

  // --- Kennwerte ---
  const kacheln = [
    ["Bauraum X · Y · Z", e.bauraum.map((v) => v.toFixed(1)).join(" × "), "mm"],
    ["Volumen", e.volumen_mm3.toLocaleString("de-DE"), "mm³"],
    ...Object.entries(e.kennwerte)
      .filter(([, v]) => typeof v === "number" || typeof v === "string")
      .map(([k, v]) => [k.replace(/_/g, " "), typeof v === "number" ? v.toLocaleString("de-DE") : v, ""]),
  ];
  $("kacheln").innerHTML = kacheln.map(([t, w, u]) =>
    `<div class="kachel"><div class="titel">${esc(t)}</div>
       <div class="wert">${esc(w)} <small>${esc(u)}</small></div></div>`).join("");
  $("karte-kennwerte").classList.remove("versteckt");

  // --- Pruefprotokoll: die Generatoren nennen die Grenze mal "grenze",
  //     mal "soll". Beides anzeigen, ohne den Python-Code anzufassen. ---
  const kopf = `<tr><th>Kriterium</th><th style="text-align:right">Wert</th>
                <th style="text-align:right">Grenze</th><th>Status</th></tr>`;
  $("pruef-tabelle").innerHTML = kopf + e.pruefung.map((c) => {
    const grenze = c.grenze !== undefined ? c.grenze : c.soll;
    const ok = c.status === "OK";
    return `<tr>
      <td>${esc(c.kriterium)}</td>
      <td class="zahl">${esc(c.wert)} ${esc(c.einheit || "")}</td>
      <td class="zahl">${esc(grenze)} ${esc(c.einheit || "")}</td>
      <td><span class="marke ${ok ? "ok" : "nicht-ok"}">${ok ? "OK" : esc(c.status)}</span></td>
    </tr>`;
  }).join("");
  $("karte-pruefung").classList.remove("versteckt");

  $("protokoll").textContent = e.log.trimEnd();
  $("karte-protokoll").classList.remove("versteckt");
}

const endung = (name) => name.split(".").pop().toLowerCase();

/** Alles zuruecksetzen, was zum vorigen Lauf gehoert. */
function ergebnisLeeren() {
  zustand.ergebnis = null;
  zustand.anzeige = null;
  ["karte-download", "karte-kennwerte", "karte-pruefung", "karte-protokoll", "viewer-fuss"]
    .forEach((id) => $(id).classList.add("versteckt"));
  $("leerhinweis").classList.remove("versteckt");
  viewer?.leeren();
  knoepfeAktualisieren();
  meldung("", "");
}

/** Modell in den Viewer laden: "bauteil" (letztes Ergebnis) oder "hairpin". */
async function meshZeigen(welches) {
  if (!viewer) return;
  const url = welches === "hairpin"
    ? `/api/hairpin-mesh?id=${encodeURIComponent($("hairpin").value)}`
    : zustand.ergebnis?.stl;
  if (!url) return;

  laden(true, welches === "hairpin" ? "Hairpin wird vernetzt …" : "Vorschau wird geladen …");
  try {
    const puffer = await hole(url);
    const daten = parseSTL(puffer);
    const mesh = viewer.setzeMesh(daten, FARBE[welches]);
    zustand.anzeige = welches;

    $("leerhinweis").classList.add("versteckt");
    const [dx, dy, dz] = [0, 1, 2].map((k) => mesh.max[k] - mesh.min[k]);
    $("viewer-fuss").classList.remove("versteckt");
    $("viewer-fuss").innerHTML = `
      <span>${welches === "hairpin" ? "Hairpin" : esc(zustand.ergebnis.titel)}</span>
      <span>Bauraum <b>${dx.toFixed(2)} × ${dy.toFixed(2)} × ${dz.toFixed(2)}</b> mm</span>
      <span>Dreiecke <b>${mesh.dreiecke.toLocaleString("de-DE")}</b></span>
      <span>Raster <b>${viewer.rasterSchritt}</b> mm</span>
      <span style="margin-left:auto">Ziehen = drehen · Rad = zoomen · Shift/Rechts = schieben</span>`;
    knoepfeAktualisieren();
  } catch (e) {
    meldung("fehler", `Vorschau fehlgeschlagen: ${e.message}`);
  } finally {
    laden(false);
  }
}

function knoepfeAktualisieren() {
  const b = $("btn-hairpin-vorschau");
  const zeigtHairpin = zustand.anzeige === "hairpin";
  b.textContent = zeigtHairpin && zustand.ergebnis ? "Zurück zum Bauteil" : "Hairpin in 3D ansehen";
  b.classList.toggle("an", zeigtHairpin);
}


// ---------------------------------------------------------------- Ereignisse
$("bauteil").addEventListener("change", () => schemaLaden().catch((e) => meldung("fehler", e.message)));
$("btn-generieren").addEventListener("click", generieren);
$("btn-zuruecksetzen").addEventListener("click", () => { formularBauen(zustand.schema); meldung("", ""); });
$("btn-neu-laden").addEventListener("click", () => hairpinsLaden().then(() => meldung("ok", "Dateiliste aktualisiert.")));

$("btn-hairpin-vorschau").addEventListener("click", () => {
  meshZeigen(zustand.anzeige === "hairpin" && zustand.ergebnis ? "bauteil" : "hairpin");
});

// --- Datei-Upload: Klick und Drag & Drop ---
$("ablage").addEventListener("click", () => $("datei-eingabe").click());
$("datei-eingabe").addEventListener("change", (e) => {
  if (e.target.files[0]) hochladen(e.target.files[0]);
  e.target.value = "";
});
["dragenter", "dragover"].forEach((n) => $("ablage").addEventListener(n, (e) => {
  e.preventDefault(); $("ablage").classList.add("aktiv");
}));
["dragleave", "drop"].forEach((n) => $("ablage").addEventListener(n, (e) => {
  e.preventDefault(); $("ablage").classList.remove("aktiv");
}));
$("ablage").addEventListener("drop", (e) => {
  if (e.dataTransfer.files[0]) hochladen(e.dataTransfer.files[0]);
});

// --- Viewer-Leiste ---
document.querySelectorAll("[data-ansicht]").forEach((b) => b.addEventListener("click", () => {
  document.querySelectorAll("[data-ansicht]").forEach((x) => x.classList.remove("an"));
  b.classList.add("an");
  viewer?.ansicht(b.dataset.ansicht);
}));
$("btn-einpassen").addEventListener("click", () => viewer?.ansichtEinpassen());
$("btn-kanten").addEventListener("click", (e) => {
  if (!viewer) return;
  viewer.zeigeKanten = !viewer.zeigeKanten;
  e.target.classList.toggle("an", viewer.zeigeKanten);
  viewer.zeichnen();
});
$("btn-raster").addEventListener("click", (e) => {
  if (!viewer) return;
  viewer.zeigeRaster = !viewer.zeigeRaster;
  e.target.classList.toggle("an", viewer.zeigeRaster);
  viewer.zeichnen();
});
$("btn-png").addEventListener("click", () => {
  if (!viewer?.mesh) return meldung("warn", "Es ist noch kein Modell geladen.");
  const a = document.createElement("a");
  a.href = viewer.screenshot();
  a.download = `${zustand.ergebnis?.stem || "vorschau"}.png`;
  a.click();
});

// Enter im Formular loest die Generierung aus
$("parameter-gruppen").addEventListener("keydown", (e) => {
  if (e.key === "Enter") { e.preventDefault(); generieren(); }
});


// ---------------------------------------------------------------- Start
(async () => {
  try {
    await bauteileLaden();
    await hairpinsLaden();
    if (!zustand.hairpins.length) {
      meldung("warn", "Keine STEP-Datei gefunden. Lege einen Hairpin als "
                    + ".step/.stp im Projektordner ab oder lade ihn hier hoch.");
    }
  } catch (e) {
    meldung("fehler", `Start fehlgeschlagen: ${e.message}`);
  }
})();
