// The web shell: load the engine, open and save circuits, drive the camera.
//
// Everything on the canvas was drawn by the same C++ that draws the desktop,
// and every pan and zoom went through the same CanvasCamera. This file supplies
// a surface, a frame loop, and a translation from DOM events. It does not
// decide how a gate looks, how far a wheel notch zooms, or what a .cdl means.

import { loadEngine, toArray, type Document, type EngineModule } from "./engine.ts";
import { replay } from "./scene.ts";

const $ = <T extends HTMLElement>(sel: string): T =>
  document.querySelector<T>(sel) ?? (() => { throw new Error(`missing ${sel}`); })();

const canvas = $<HTMLCanvasElement>("#canvas");
const statusEl = $<HTMLDivElement>("#status");
const readoutEl = $<HTMLDivElement>("#readout");
const gatesEl = $<HTMLUListElement>("#gates");
const filterEl = $<HTMLInputElement>("#filter");
const fileEl = $<HTMLInputElement>("#file");
const hintEl = $<HTMLDivElement>("#hint");
const mainEl = $<HTMLElement>("main");

function setStatus(text: string, isError = false): void {
  statusEl.textContent = text;
  statusEl.classList.toggle("error", isError);
}

// ─── frame loop ──────────────────────────────────────────────────────────────

/** Draws when the document says something changed, and not otherwise. */
function startFrameLoop(module: EngineModule, doc: Document): void {
  const ctx = canvas.getContext("2d");
  if (!ctx) throw new Error("no 2d context");

  let lastW = -1;
  let lastH = -1;

  const frame = (): void => {
    const ratio = window.devicePixelRatio || 1;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;

    if (width > 0 && height > 0 && (width !== lastW || height !== lastH)) {
      lastW = width;
      lastH = height;
      canvas.width = Math.round(width * ratio);
      canvas.height = Math.round(height * ratio);
      // The camera measures in CSS pixels; the device ratio is the renderer's
      // business, handed to render() below.
      doc.setViewportSize(width, height);
    }

    if (doc.isDirty() && width > 0 && height > 0) {
      doc.render(ratio);

      const cmds = new Float32Array(module.HEAPF32.buffer, doc.sceneData(), doc.sceneLength());
      const strings = toArray(doc.sceneStrings());

      ctx.fillStyle = doc.background();
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      // The stream is already in device pixels (render() got the ratio), so the
      // replayer must not scale it again.
      replay(ctx, cmds, strings, { pixelRatio: 1, font: "ui-monospace, monospace" });

      updateReadout(doc);
    }

    requestAnimationFrame(frame);
  };

  requestAnimationFrame(frame);
}

function updateReadout(doc: Document): void {
  const zoom = doc.getZoom();
  // Zoom is world units per pixel, so a smaller number is closer in. Show the
  // reciprocal, which is what a person means by "200%".
  const percent = Math.round((1 / zoom) * 10);
  readoutEl.textContent = `${doc.gateCount()} gates · ${doc.wireCount()} wires · ${percent}%`;
}

// ─── camera ──────────────────────────────────────────────────────────────────

function bindCamera(doc: Document): void {
  // A wheel notch is one zoom step pivoting on the cursor -- the desktop's
  // zoomToMouse, reached through the same camera.
  canvas.addEventListener(
    "wheel",
    (e) => {
      e.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const px = Math.round(e.clientX - rect.left);
      const py = Math.round(e.clientY - rect.top);

      // A trackpad's two-finger drag arrives as a wheel event with no ctrlKey
      // and small deltas; treat that as panning, which is what the gesture
      // means, and reserve zooming for a real wheel or a pinch (ctrlKey).
      const isPinch = e.ctrlKey;
      const isTrackpadPan = !isPinch && e.deltaMode === 0 && Math.abs(e.deltaY) < 30 && e.deltaX !== 0;

      if (isTrackpadPan) {
        const zoom = doc.getZoom();
        doc.translatePan(e.deltaX * zoom, -e.deltaY * zoom);
        return;
      }
      doc.zoomAt(e.deltaY < 0 ? 1 : -1, px, py);
    },
    { passive: false },
  );

  // Middle-drag pans, as on the desktop. Space-drag does too, since a browser
  // cannot count on a middle button being there.
  let panning = false;
  let lastX = 0;
  let lastY = 0;
  let spaceHeld = false;

  canvas.addEventListener("pointerdown", (e) => {
    canvas.focus();
    if (e.button !== 1 && !(e.button === 0 && spaceHeld)) return;
    e.preventDefault();
    panning = true;
    lastX = e.clientX;
    lastY = e.clientY;
    canvas.setPointerCapture(e.pointerId);
  });

  canvas.addEventListener("pointermove", (e) => {
    if (!panning) return;
    // Drag the world with the cursor: the view moves the opposite way, and one
    // CSS pixel of travel is one pixel of world at any zoom.
    const zoom = doc.getZoom();
    doc.translatePan(-(e.clientX - lastX) * zoom, (e.clientY - lastY) * zoom);
    lastX = e.clientX;
    lastY = e.clientY;
  });

  const endPan = (e: PointerEvent): void => {
    if (!panning) return;
    panning = false;
    if (canvas.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
  };
  canvas.addEventListener("pointerup", endPan);
  canvas.addEventListener("pointercancel", endPan);

  window.addEventListener("keydown", (e) => {
    // Never steal a key from a text field.
    if (e.target instanceof HTMLInputElement) return;

    if (e.code === "Space") {
      spaceHeld = true;
      if (!panning) {
        e.preventDefault();
        doc.zoomAll();
      }
      return;
    }
    if (e.key === "+" || e.key === "=") { e.preventDefault(); doc.zoomAt(1, canvas.clientWidth / 2, canvas.clientHeight / 2); }
    if (e.key === "-") { e.preventDefault(); doc.zoomAt(-1, canvas.clientWidth / 2, canvas.clientHeight / 2); }
  });
  window.addEventListener("keyup", (e) => {
    if (e.code === "Space") spaceHeld = false;
  });
}

// ─── files ───────────────────────────────────────────────────────────────────

function reportLoad(doc: Document, name: string, error: string): void {
  if (error) {
    setStatus(`${name}: ${error}`, true);
    return;
  }
  const notices = toArray(doc.loadNotices());
  if (notices.length > 0) {
    // A migrated file may have lost something; say so rather than pretending
    // the read was clean.
    setStatus(`${name} — ${notices.length} note${notices.length === 1 ? "" : "s"}:\n${notices.join("\n")}`);
  } else {
    setStatus(`${name} — ${doc.gateCount()} gates, ${doc.wireCount()} wires`);
  }
  doc.zoomAll();
}

async function openFile(doc: Document, file: File): Promise<void> {
  try {
    reportLoad(doc, file.name, doc.loadCircuit(await file.text()));
  } catch (err) {
    setStatus(`${file.name}: ${String(err)}`, true);
  }
}

function bindFiles(doc: Document): void {
  $<HTMLButtonElement>("#open").addEventListener("click", () => fileEl.click());

  fileEl.addEventListener("change", () => {
    const file = fileEl.files?.[0];
    if (file) void openFile(doc, file);
    // Clear it, or picking the same file twice fires no change event.
    fileEl.value = "";
  });

  $<HTMLButtonElement>("#save").addEventListener("click", () => {
    const blob = new Blob([doc.saveCircuit()], { type: "text/plain" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "circuit.cdl";
    a.click();
    URL.revokeObjectURL(url);
    setStatus("saved circuit.cdl");
  });

  $<HTMLButtonElement>("#clear").addEventListener("click", () => {
    doc.clearCircuit();
    doc.zoomAll();
    setStatus("new circuit");
  });

  // Drag and drop, because that is how anyone actually opens a file they are
  // looking at.
  for (const type of ["dragenter", "dragover"]) {
    mainEl.addEventListener(type, (e) => {
      e.preventDefault();
      mainEl.classList.add("dragover");
    });
  }
  for (const type of ["dragleave", "drop"]) {
    mainEl.addEventListener(type, (e) => {
      e.preventDefault();
      mainEl.classList.remove("dragover");
    });
  }
  mainEl.addEventListener("drop", (e) => {
    const file = (e as DragEvent).dataTransfer?.files?.[0];
    if (file) void openFile(doc, file);
  });
}

// ─── palette ─────────────────────────────────────────────────────────────────

function buildPalette(doc: Document, types: readonly string[]): void {
  const rows: { name: string; item: HTMLLIElement }[] = [];

  for (const type of types) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = type;
    button.addEventListener("click", () => {
      // Place into the middle of whatever the camera is looking at, so a gate
      // never lands somewhere the user cannot see.
      const cx = canvas.clientWidth / 2;
      const cy = canvas.clientHeight / 2;
      if (doc.addGate(type, doc.worldX(cx, cy), doc.worldY(cx, cy)) < 0) {
        setStatus(`${type}: not in the library`, true);
        return;
      }
      setStatus(`placed ${type}`);
    });
    item.append(button);
    gatesEl.append(item);
    rows.push({ name: type.toLowerCase(), item });
  }

  filterEl.addEventListener("input", () => {
    const q = filterEl.value.trim().toLowerCase();
    for (const row of rows) {
      row.item.hidden = q.length > 0 && !row.name.includes(q);
    }
  });
}

// ─── main ────────────────────────────────────────────────────────────────────

async function main(): Promise<void> {
  setStatus("loading engine…");

  let module: EngineModule;
  try {
    module = await loadEngine();
  } catch (err) {
    setStatus(`engine failed to load\n${String(err)}`, true);
    return;
  }

  const doc = new module.Document();
  const loadError = doc.loadError();
  if (loadError) {
    setStatus(`gate library: ${loadError}`, true);
    return;
  }

  const types = toArray(doc.gateTypes());
  setStatus(`${types.length} gate types · open a .cdl or drop one here`);

  buildPalette(doc, types);
  bindCamera(doc);
  bindFiles(doc);
  startFrameLoop(module, doc);

  // The hint has done its job once the view has been touched.
  const fadeHint = (): void => hintEl.classList.add("faded");
  canvas.addEventListener("wheel", fadeHint, { once: true, passive: true });
  canvas.addEventListener("pointerdown", fadeHint, { once: true });

  requestAnimationFrame(() => doc.zoomAll());
}

void main();
