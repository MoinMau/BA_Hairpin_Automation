/*
 * viewer.js -- Minimaler STL-Viewer auf rohem WebGL.
 *
 * Bewusst ohne three.js o.ae.: keine CDN-Abhaengigkeit, damit die Oberflaeche
 * auch ohne Internet laeuft. Enthalten ist nur, was fuer eine CAD-Vorschau
 * wirklich gebraucht wird:
 *
 *   - Binaeres und ASCII-STL einlesen
 *   - Flat Shading mit zwei Lichtern (Facetten bleiben sichtbar -- gewollt,
 *     man sieht der Vorschau die Tesselierung an)
 *   - Orbit / Zoom / Pan, Z-Achse zeigt nach oben wie im CAD
 *   - Bodenraster und Achsenkreuz zur Groessenorientierung
 */

// --- 4x4-Matrizen, spaltenweise wie in OpenGL ------------------------------
const M4 = {
  perspective(fovy, aspect, near, far) {
    const f = 1 / Math.tan(fovy / 2), nf = 1 / (near - far);
    return [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0];
  },
  lookAt(eye, center, up) {
    const z = norm(sub(eye, center));
    const x = norm(cross(up, z));
    const y = cross(z, x);
    return [x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
            -dot(x, eye), -dot(y, eye), -dot(z, eye), 1];
  },
  mul(a, b) {                       // a * b
    const o = new Array(16);
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
      o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] +
                     a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    }
    return o;
  },
};
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };


// --- STL einlesen ----------------------------------------------------------
/**
 * Liefert {positions, normals, triangles, min, max}.
 * Die Normalen werden aus den Eckpunkten neu berechnet -- die im STL
 * hinterlegten sind je nach Exporter Null oder falsch orientiert.
 */
export function parseSTL(buffer) {
  const view = new DataView(buffer);
  let tris = null;

  if (buffer.byteLength >= 84) {
    const anzahl = view.getUint32(80, true);
    if (84 + anzahl * 50 === buffer.byteLength) tris = leseBinaer(view, anzahl);
  }
  if (tris === null) tris = leseAscii(new TextDecoder().decode(buffer));
  if (!tris || tris.length === 0) throw new Error("STL enthaelt keine Dreiecke.");

  const positions = new Float32Array(tris.length * 9);
  const normals = new Float32Array(tris.length * 9);
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];

  tris.forEach((t, i) => {
    // Flaechenlose Dreiecke kommen in tesselierten Netzen vor. Ihre Normale
    // waere ein Nullvektor und im Shader NaN -- daher ein Ersatzwert.
    const roh = cross(sub(t[1], t[0]), sub(t[2], t[0]));
    const n = Math.hypot(roh[0], roh[1], roh[2]) > 1e-12 ? norm(roh) : [0, 0, 1];
    for (let e = 0; e < 3; e++) {
      const o = i * 9 + e * 3;
      positions[o] = t[e][0]; positions[o + 1] = t[e][1]; positions[o + 2] = t[e][2];
      normals[o] = n[0]; normals[o + 1] = n[1]; normals[o + 2] = n[2];
      for (let k = 0; k < 3; k++) {
        if (t[e][k] < min[k]) min[k] = t[e][k];
        if (t[e][k] > max[k]) max[k] = t[e][k];
      }
    }
  });
  return { positions, normals, triangles: tris.length, min, max };
}

function leseBinaer(view, anzahl) {
  const tris = [];
  for (let i = 0; i < anzahl; i++) {
    const o = 84 + i * 50 + 12;          // 12 Byte Normale ueberspringen
    tris.push([0, 1, 2].map((e) => [
      view.getFloat32(o + e * 12, true),
      view.getFloat32(o + e * 12 + 4, true),
      view.getFloat32(o + e * 12 + 8, true),
    ]));
  }
  return tris;
}

function leseAscii(text) {
  const tris = [];
  const re = /vertex\s+(-?[\d.eE+-]+)\s+(-?[\d.eE+-]+)\s+(-?[\d.eE+-]+)/g;
  let m, ecken = [];
  while ((m = re.exec(text)) !== null) {
    ecken.push([parseFloat(m[1]), parseFloat(m[2]), parseFloat(m[3])]);
    if (ecken.length === 3) { tris.push(ecken); ecken = []; }
  }
  return tris;
}


// --- Shader ----------------------------------------------------------------
const VS_KOERPER = `
attribute vec3 aPos;
attribute vec3 aNormal;
uniform mat4 uMVP;
varying vec3 vNormal;
varying vec3 vPos;
void main() {
  vNormal = aNormal;
  vPos = aPos;
  gl_Position = uMVP * vec4(aPos, 1.0);
}`;

const FS_KOERPER = `
precision mediump float;
varying vec3 vNormal;
varying vec3 vPos;
uniform vec3 uKamera;
uniform vec3 uFarbe;
void main() {
  vec3 N = normalize(vNormal);
  vec3 V = normalize(uKamera - vPos);
  if (dot(N, V) < 0.0) N = -N;                 // Rueckseiten mitbeleuchten
  vec3 L1 = normalize(vec3(0.45, 0.65, 1.0));
  vec3 L2 = normalize(vec3(-0.7, -0.35, 0.25));
  float diffus = max(dot(N, L1), 0.0) * 0.80 + max(dot(N, L2), 0.0) * 0.28;
  vec3 H = normalize(L1 + V);
  float glanz = pow(max(dot(N, H), 0.0), 40.0) * 0.30;
  float kante = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.20;
  gl_FragColor = vec4(uFarbe * (0.24 + diffus) + glanz + kante, 1.0);
}`;

const VS_LINIE = `
attribute vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }`;

const FS_LINIE = `
precision mediump float;
uniform vec4 uFarbe;
void main() { gl_FragColor = uFarbe; }`;


// --- Viewer ----------------------------------------------------------------
export class Viewer {
  constructor(canvas) {
    this.canvas = canvas;
    const opts = { antialias: true, alpha: false, preserveDrawingBuffer: true };
    this.gl = canvas.getContext("webgl", opts) || canvas.getContext("experimental-webgl", opts);
    if (!this.gl) throw new Error("WebGL wird von diesem Browser nicht unterstuetzt.");

    const gl = this.gl;
    this.progKoerper = this._programm(VS_KOERPER, FS_KOERPER);
    this.progLinie = this._programm(VS_LINIE, FS_LINIE);
    this.buffers = {
      pos: gl.createBuffer(), normal: gl.createBuffer(),
      kante: gl.createBuffer(), raster: gl.createBuffer(),
    };

    this.mesh = null;          // {anzahl, min, max, kantenAnzahl}
    this.rasterAnzahl = 0;
    this.farbe = [0.42, 0.68, 0.95];
    this.zeigeRaster = true;
    this.zeigeKanten = false;

    // Kamera in Kugelkoordinaten um "ziel"; Z ist oben wie im CAD.
    this.ziel = [0, 0, 0];
    this.theta = -Math.PI * 0.35;
    this.phi = Math.PI * 0.36;
    this.radius = 100;

    this._eingabeBinden();
    this._groesseAnpassen();
    new ResizeObserver(() => { this._groesseAnpassen(); this.zeichnen(); }).observe(canvas);
  }

  _programm(vsQuelle, fsQuelle) {
    const gl = this.gl;
    const bauen = (typ, quelle) => {
      const s = gl.createShader(typ);
      gl.shaderSource(s, quelle);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        throw new Error("Shader-Fehler: " + gl.getShaderInfoLog(s));
      }
      return s;
    };
    const p = gl.createProgram();
    gl.attachShader(p, bauen(gl.VERTEX_SHADER, vsQuelle));
    gl.attachShader(p, bauen(gl.FRAGMENT_SHADER, fsQuelle));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
      throw new Error("Link-Fehler: " + gl.getProgramInfoLog(p));
    }
    return p;
  }

  _groesseAnpassen() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const b = Math.max(1, Math.round(this.canvas.clientWidth * dpr));
    const h = Math.max(1, Math.round(this.canvas.clientHeight * dpr));
    if (this.canvas.width !== b || this.canvas.height !== h) {
      this.canvas.width = b; this.canvas.height = h;
    }
  }

  // --- Maus / Touch ---------------------------------------------------
  _eingabeBinden() {
    const c = this.canvas;
    let letzte = null, modus = null;

    const start = (e) => {
      const p = zeiger(e);
      letzte = p;
      modus = (e.button === 2 || e.shiftKey || (e.touches && e.touches.length === 2)) ? "pan" : "orbit";
      c.setPointerCapture?.(e.pointerId);
    };
    const bewegen = (e) => {
      if (!letzte) return;
      const p = zeiger(e);
      const dx = p.x - letzte.x, dy = p.y - letzte.y;
      letzte = p;
      if (modus === "orbit") {
        this.theta -= dx * 0.008;
        this.phi = Math.min(Math.PI - 0.02, Math.max(0.02, this.phi - dy * 0.008));
      } else {
        // Verschiebung in der Bildebene, skaliert mit der Entfernung --
        // so bleibt das Panning bei jedem Zoom gleich schnell.
        const { rechts, oben } = this._achsen();
        const f = this.radius * 0.0022;
        for (let k = 0; k < 3; k++) this.ziel[k] += (-rechts[k] * dx + oben[k] * dy) * f;
      }
      e.preventDefault();
      this.zeichnen();
    };
    const ende = () => { letzte = null; modus = null; };
    const zeiger = (e) => (e.touches ? { x: e.touches[0].clientX, y: e.touches[0].clientY }
                                     : { x: e.clientX, y: e.clientY });

    c.addEventListener("pointerdown", start);
    c.addEventListener("pointermove", bewegen);
    c.addEventListener("pointerup", ende);
    c.addEventListener("pointercancel", ende);
    c.addEventListener("contextmenu", (e) => e.preventDefault());
    c.addEventListener("wheel", (e) => {
      e.preventDefault();
      this.radius = Math.max(0.5, this.radius * Math.exp(e.deltaY * 0.0012));
      this.zeichnen();
    }, { passive: false });
  }

  _achsen() {
    const auge = this._auge();
    const vor = norm(sub(this.ziel, auge));
    const rechts = norm(cross(vor, [0, 0, 1]));
    return { vor, rechts, oben: cross(rechts, vor) };
  }

  _auge() {
    const s = Math.sin(this.phi);
    return [
      this.ziel[0] + this.radius * s * Math.cos(this.theta),
      this.ziel[1] + this.radius * s * Math.sin(this.theta),
      this.ziel[2] + this.radius * Math.cos(this.phi),
    ];
  }

  // --- Geometrie setzen -----------------------------------------------
  setzeMesh(daten, farbe) {
    const gl = this.gl;
    if (farbe) this.farbe = farbe;

    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.pos);
    gl.bufferData(gl.ARRAY_BUFFER, daten.positions, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.normal);
    gl.bufferData(gl.ARRAY_BUFFER, daten.normals, gl.STATIC_DRAW);

    // Dreieckskanten fuer die Drahtgitter-Ansicht
    const kanten = new Float32Array(daten.triangles * 18);
    for (let i = 0; i < daten.triangles; i++) {
      const s = i * 9, z = i * 18;
      for (let e = 0; e < 3; e++) {
        const a = s + e * 3, b = s + ((e + 1) % 3) * 3;
        kanten.set(daten.positions.subarray(a, a + 3), z + e * 6);
        kanten.set(daten.positions.subarray(b, b + 3), z + e * 6 + 3);
      }
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.kante);
    gl.bufferData(gl.ARRAY_BUFFER, kanten, gl.STATIC_DRAW);

    this.mesh = {
      anzahl: daten.triangles * 3, kantenAnzahl: daten.triangles * 6,
      min: daten.min, max: daten.max, dreiecke: daten.triangles,
    };
    this._rasterBauen();
    this.ansichtEinpassen();
    return this.mesh;
  }

  leeren() { this.mesh = null; this.zeichnen(); }

  /** Bodenraster mit runder Teilung, plus Achsenkreuz im Ursprung. */
  _rasterBauen() {
    const { min, max } = this.mesh;
    const spanne = Math.max(max[0] - min[0], max[1] - min[1], 1);
    const roh = spanne / 8;
    const stufe = Math.pow(10, Math.floor(Math.log10(roh)));
    const schritt = [1, 2, 5, 10].map((f) => f * stufe).find((s) => s >= roh) || stufe * 10;
    const n = 8;
    const w = n * schritt;
    const z = min[2];
    const cx = Math.round((min[0] + max[0]) / 2 / schritt) * schritt;
    const cy = Math.round((min[1] + max[1]) / 2 / schritt) * schritt;

    const v = [];
    for (let i = -n; i <= n; i++) {
      v.push(cx - w, cy + i * schritt, z, cx + w, cy + i * schritt, z);
      v.push(cx + i * schritt, cy - w, z, cx + i * schritt, cy + w, z);
    }
    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.raster);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(v), gl.STATIC_DRAW);
    this.rasterAnzahl = v.length / 3;
    this.rasterSchritt = schritt;
  }

  ansichtEinpassen() {
    if (!this.mesh) return;
    const { min, max } = this.mesh;
    this.ziel = [0, 1, 2].map((k) => (min[k] + max[k]) / 2);
    const diagonale = Math.hypot(max[0] - min[0], max[1] - min[1], max[2] - min[2]);
    this.radius = Math.max(diagonale * 1.6, 1);
    this.zeichnen();
  }

  ansicht(name) {
    const winkel = {
      iso: [-Math.PI * 0.35, Math.PI * 0.36],
      vorne: [-Math.PI / 2, Math.PI / 2],
      seite: [0, Math.PI / 2],
      oben: [-Math.PI / 2, 0.02],
    }[name];
    if (winkel) { [this.theta, this.phi] = winkel; this.zeichnen(); }
  }

  // --- Zeichnen --------------------------------------------------------
  zeichnen() {
    const gl = this.gl;
    this._groesseAnpassen();
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.086, 0.098, 0.125, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    if (!this.mesh) return;

    const auge = this._auge();
    const nah = Math.max(this.radius / 200, 0.01);
    const proj = M4.perspective(0.7, this.canvas.width / this.canvas.height, nah, this.radius * 50);
    const mvp = M4.mul(proj, M4.lookAt(auge, this.ziel, [0, 0, 1]));

    if (this.zeigeRaster && this.rasterAnzahl) {
      this._linienZeichnen(mvp, this.buffers.raster, this.rasterAnzahl, [1, 1, 1, 0.09]);
    }

    gl.useProgram(this.progKoerper);
    this._attribute(this.progKoerper, [["aPos", this.buffers.pos],
                                       ["aNormal", this.buffers.normal]]);
    gl.uniformMatrix4fv(gl.getUniformLocation(this.progKoerper, "uMVP"), false, new Float32Array(mvp));
    gl.uniform3fv(gl.getUniformLocation(this.progKoerper, "uKamera"), new Float32Array(auge));
    gl.uniform3fv(gl.getUniformLocation(this.progKoerper, "uFarbe"), new Float32Array(this.farbe));
    // Flaechen minimal nach hinten schieben, sonst kaempfen sie in der
    // Kantenansicht mit den deckungsgleichen Linien um die Tiefe.
    gl.enable(gl.POLYGON_OFFSET_FILL);
    gl.polygonOffset(1.0, 1.0);
    gl.drawArrays(gl.TRIANGLES, 0, this.mesh.anzahl);
    gl.disable(gl.POLYGON_OFFSET_FILL);

    if (this.zeigeKanten) {
      this._linienZeichnen(mvp, this.buffers.kante, this.mesh.kantenAnzahl, [0.03, 0.05, 0.08, 0.55]);
    }
  }

  _linienZeichnen(mvp, buffer, anzahl, farbe) {
    const gl = this.gl;
    gl.useProgram(this.progLinie);
    this._attribute(this.progLinie, [["aPos", buffer]]);
    gl.uniformMatrix4fv(gl.getUniformLocation(this.progLinie, "uMVP"), false, new Float32Array(mvp));
    gl.uniform4fv(gl.getUniformLocation(this.progLinie, "uFarbe"), new Float32Array(farbe));
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.drawArrays(gl.LINES, 0, anzahl);
    gl.disable(gl.BLEND);
  }

  /* Attribute des Programms aktivieren und alle uebrigen abschalten.
     Das Abschalten ist Pflicht: WebGL 1 kennt kein VAO, der Zustand gilt
     global. Ein aktiv gebliebenes aNormal wuerde beim Zeichnen des Rasters
     mitgelesen -- und bei zu kurzem Puffer den Draw-Call verwerfen. */
  _attribute(programm, paare) {
    const gl = this.gl;
    const jetzt = new Set();
    for (const [name, buffer] of paare) {
      const ort = gl.getAttribLocation(programm, name);
      if (ort < 0) continue;
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.enableVertexAttribArray(ort);
      gl.vertexAttribPointer(ort, 3, gl.FLOAT, false, 0, 0);
      jetzt.add(ort);
    }
    for (const ort of this._aktiv || []) if (!jetzt.has(ort)) gl.disableVertexAttribArray(ort);
    this._aktiv = jetzt;
  }

  /** Aktuelles Bild als PNG -- praktisch fuer die Dokumentation. */
  screenshot() {
    this.zeichnen();                 // frischer Puffer, sonst evtl. leer
    return this.canvas.toDataURL("image/png");
  }
}
