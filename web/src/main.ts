// The web shell: load the engine, open and save circuits, drive the camera.
//
// Everything on the canvas was drawn by the same C++ that draws the desktop,
// and every pan and zoom went through the same CanvasCamera. This file supplies
// a surface, a frame loop, and a translation from DOM events. It does not
// decide how a gate looks, how far a wheel notch zooms, or what a .cdl means.

import { loadEngine, toArray, type Document, type EngineModule } from "./engine.ts";
import { replay } from "./scene.ts";
import { Palette } from "./palette.ts";
import { ParamEditor } from "./params.ts";
import { wheelSteps } from "./wheel.ts";

const $ = <T extends HTMLElement>(sel: string): T =>
  document.querySelector<T>(sel) ?? (() => { throw new Error(`missing ${sel}`); })();

const canvas = $<HTMLCanvasElement>("#canvas");
const statusEl = $<HTMLDivElement>("#status");
const readoutEl = $<HTMLDivElement>("#readout");
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
  let lastFrameTime = performance.now();
  let drawMs = 0;

  const frame = (): void => {
    const now = performance.now();
    const elapsed = now - lastFrameTime;
    lastFrameTime = now;

    // The desktop runs the core on its own thread and pumps it from a timer;
    // here the frame loop is the timer. The engine caps a long gap itself, so a
    // backgrounded tab resumes rather than racing to catch up.
    doc.stepSimulation(elapsed);

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
      const drawStart = performance.now();
      doc.render(ratio);

      const cmds = new Float32Array(module.HEAPF32.buffer, doc.sceneData(), doc.sceneLength());
      const strings = toArray(doc.sceneStrings());

      ctx.fillStyle = doc.background();
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      // The stream is already in device pixels (render() got the ratio), so the
      // replayer must not scale it again.
      replay(ctx, cmds, strings, { pixelRatio: 1, font: "ui-monospace, monospace" });

      // A rolling average of the frames that actually drew, so the readout
      // reports the cost of a redraw rather than the idle gaps between them.
      drawMs = drawMs * 0.9 + (performance.now() - drawStart) * 0.1;
      updateReadout(doc, drawMs);
    }

    requestAnimationFrame(frame);
  };

  requestAnimationFrame(frame);
}

// The simulation controls. Pausing is the desktop's Pause button; the engine
// also stops itself if a circuit cannot keep up, which shows here rather than
// leaving the view mysteriously frozen.
function bindSimulation(doc: Document): void {
  const runBtn = $<HTMLButtonElement>("#run");
  const stepBtn = $<HTMLButtonElement>("#step");

  const sync = (): void => {
    const running = doc.isSimulating();
    runBtn.textContent = running ? "Pause" : "Run";
    runBtn.classList.toggle("running", running);
    runBtn.classList.toggle("panicked", doc.inPanic());
  };

  runBtn.addEventListener("click", () => {
    if (doc.inPanic()) doc.clearPanic();
    doc.setSimulating(!doc.isSimulating());
    sync();
    setStatus(doc.isSimulating() ? "running" : "paused");
  });

  stepBtn.addEventListener("click", () => {
    doc.setSimulating(false);
    doc.stepOnce();
    sync();
    setStatus("stepped");
  });

  // The core can stop itself, so the button cannot only follow clicks.
  setInterval(sync, 250);
  sync();
}

function updateReadout(doc: Document, drawMs: number): void {
  const zoom = doc.getZoom();
  // Zoom is world units per pixel, so a smaller number is closer in. Show the
  // reciprocal, which is what a person means by "200%".
  const percent = Math.round((1 / zoom) * 10);
  const selected = doc.selectedCount();
  readoutEl.textContent =
    `${doc.gateCount()} gates · ${doc.wireCount()} wires` +
    (selected > 0 ? ` · ${selected} selected` : "") +
    ` · ${percent}%` +
    (drawMs >= 0.05 ? ` · ${drawMs.toFixed(1)}ms` : "");
}

// ─── camera ──────────────────────────────────────────────────────────────────

function bindInput(doc: Document): void {
  const at = (e: { clientX: number; clientY: number }): [number, number] => {
    const r = canvas.getBoundingClientRect();
    return [Math.round(e.clientX - r.left), Math.round(e.clientY - r.top)];
  };
  const mods = (e: MouseEvent | KeyboardEvent): [boolean, boolean, boolean, boolean] =>
    [e.shiftKey, e.ctrlKey, e.altKey, e.metaKey];

  // The wheel, as klsGLCanvas::wxOnMouseWheel handles it:
  //
  //   plain scroll   zoom about the cursor
  //   Cmd            pan, in the direction of the scroll -- including a
  //                  two-finger trackpad drag, which is how you translate
  //   Shift          pan horizontally, for a mouse with only one wheel
  //
  // The desktop rounds rotation into whole steps before acting. That is right
  // for a mouse, where a notch is a notch, but it is what makes trackpad zoom
  // lurch -- so the fraction is passed through instead and the camera zooms by
  // it. A notch is still worth exactly one step either way.
  //
  // The one deviation is Ctrl. The desktop pans vertically with it, but a
  // browser reports a trackpad pinch as a Ctrl-held wheel event and there is no
  // way to tell the two apart -- so Ctrl zooms here, and pinch works.
  canvas.addEventListener(
    "wheel",
    (e) => {
      e.preventDefault();
      const [px, py] = at(e);
      const steps = wheelSteps(e);

      if (e.metaKey) {
        doc.scrollPan(steps.x, steps.y);
        return;
      }
      if (e.shiftKey) {
        // The desktop turns vertical rotation into horizontal pan here.
        doc.scrollPan(steps.y, 0);
        return;
      }
      if (steps.y !== 0) doc.zoomAtBy(steps.y, px, py);
    },
    { passive: false },
  );

  // Panning is the shell's own gesture: middle-drag as on the desktop, and
  // space-drag too, since a browser cannot count on a middle button. Everything
  // else goes to the page.
  let panning = false;
  let lastX = 0;
  let lastY = 0;
  let spaceHeld = false;

  canvas.addEventListener("pointerdown", (e) => {
    canvas.focus();
    const [px, py] = at(e);

    if (e.button === 1 || (e.button === 0 && spaceHeld)) {
      e.preventDefault();
      panning = true;
      lastX = e.clientX;
      lastY = e.clientY;
      canvas.setPointerCapture(e.pointerId);
      return;
    }

    e.preventDefault();
    canvas.setPointerCapture(e.pointerId);
    doc.pointerDown(px, py, e.button, ...mods(e));
  });

  canvas.addEventListener("pointermove", (e) => {
    if (panning) {
      // Drag the world with the cursor: the view moves the opposite way, and
      // one CSS pixel of travel is one pixel of world at any zoom.
      const zoom = doc.getZoom();
      doc.translatePan(-(e.clientX - lastX) * zoom, (e.clientY - lastY) * zoom);
      lastX = e.clientX;
      lastY = e.clientY;
      return;
    }
    const [px, py] = at(e);
    doc.pointerMove(px, py, (e.buttons & 1) !== 0, ...mods(e));
  });

  const release = (e: PointerEvent, doubleClick = false): void => {
    if (canvas.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
    if (panning) {
      panning = false;
      return;
    }
    const [px, py] = at(e);
    doc.pointerUp(px, py, e.button, doubleClick, ...mods(e));
  };

  canvas.addEventListener("pointerup", (e) => release(e));
  canvas.addEventListener("pointercancel", (e) => {
    if (canvas.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
    panning = false;
    doc.keyDown("Escape", false, false, false, false);
  });
  canvas.addEventListener("dblclick", (e) => {
    const [px, py] = at(e);
    doc.pointerUp(px, py, e.button, true, ...mods(e));
  });

  // A right-click is a circuit gesture here, not a browser one.
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());

  window.addEventListener("keydown", (e) => {
    // Never steal a key from a text field.
    if (e.target instanceof HTMLInputElement) return;

    if (e.code === "Space") spaceHeld = true;

    // The shell owns the clipboard and the undo shortcuts, because those are
    // platform conventions rather than circuit behaviour.
    const accel = e.metaKey || e.ctrlKey;
    if (accel) {
      const key = e.key.toLowerCase();
      if (key === "z" && !e.shiftKey) { e.preventDefault(); doc.undo(); return; }
      if ((key === "z" && e.shiftKey) || key === "y") { e.preventDefault(); doc.redo(); return; }
      if (key === "c") { e.preventDefault(); doc.copySelection(); syncClipboardOut(doc); return; }
      if (key === "x") { e.preventDefault(); doc.cutSelection(); syncClipboardOut(doc); return; }
      if (key === "v") { e.preventDefault(); void pasteFromSystem(doc); return; }
      if (key === "s") { e.preventDefault(); saveCircuit(doc); return; }
      if (key === "o") { e.preventDefault(); fileEl.click(); return; }
      return;
    }

    // Everything else is the page's: delete, escape, arrows, zoom, r, a.
    e.preventDefault();
    doc.keyDown(e.key, ...mods(e));
    if (doc.takeQuickAddRequest()) filterEl.focus();
  });

  window.addEventListener("keyup", (e) => {
    if (e.code === "Space") spaceHeld = false;
  });
}

// The page's clipboard is in-process, because the browser's is asynchronous and
// needs a gesture. These move text between the two around one.
function syncClipboardOut(doc: Document): void {
  const text = doc.clipboardText();
  if (text) void navigator.clipboard?.writeText(text).catch(() => {});
}

async function pasteFromSystem(doc: Document): Promise<void> {
  try {
    const text = await navigator.clipboard?.readText();
    if (text) doc.setClipboardText(text);
  } catch {
    // No permission, or no clipboard: fall back to whatever we copied here.
  }
  doc.paste();
}

// ─── files ───────────────────────────────────────────────────────────────────

function saveCircuit(doc: Document): void {
  const blob = new Blob([doc.saveCircuit()], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "circuit.cdl";
  a.click();
  URL.revokeObjectURL(url);
  setStatus("saved circuit.cdl");
}

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

function bindToolbar(doc: Document): void {
  const undoBtn = $<HTMLButtonElement>("#undo");
  const redoBtn = $<HTMLButtonElement>("#redo");
  const cutBtn = $<HTMLButtonElement>("#cut");
  const copyBtn = $<HTMLButtonElement>("#copy");
  const lockBtn = $<HTMLButtonElement>("#lock");
  const stepEl = $<HTMLInputElement>("#timestep");
  const stepVal = $<HTMLOutputElement>("#timestepval");
  const gridEl = $<HTMLInputElement>("#showgrid");
  const connEl = $<HTMLInputElement>("#showconn");

  undoBtn.addEventListener("click", () => { doc.undo(); syncToolbar(doc); });
  redoBtn.addEventListener("click", () => { doc.redo(); syncToolbar(doc); });
  cutBtn.addEventListener("click", () => { doc.cutSelection(); syncClipboardOut(doc); syncToolbar(doc); });
  copyBtn.addEventListener("click", () => { doc.copySelection(); syncClipboardOut(doc); });
  $<HTMLButtonElement>("#paste").addEventListener("click", () => void pasteFromSystem(doc));

  const centre = (): [number, number] => [canvas.clientWidth / 2, canvas.clientHeight / 2];
  $<HTMLButtonElement>("#zoomin").addEventListener("click", () => doc.zoomAt(1, ...centre()));
  $<HTMLButtonElement>("#zoomout").addEventListener("click", () => doc.zoomAt(-1, ...centre()));
  $<HTMLButtonElement>("#zoomfit").addEventListener("click", () => doc.zoomAll());

  // The desktop's lock leaves the simulation running and turns editing off.
  lockBtn.addEventListener("click", () => {
    doc.setLocked(!doc.isLocked());
    setStatus(doc.isLocked() ? "locked: editing disabled" : "unlocked");
    syncToolbar(doc);
  });

  stepEl.value = String(doc.timeStep());
  const showStep = (): void => { stepVal.textContent = `${stepEl.value} ms`; };
  stepEl.addEventListener("input", () => {
    doc.setTimeStep(Number(stepEl.value));
    showStep();
  });
  showStep();

  gridEl.checked = doc.gridlinesVisible();
  connEl.checked = doc.wireConnectionsVisible();
  gridEl.addEventListener("change", () => doc.setGridlinesVisible(gridEl.checked));
  connEl.addEventListener("change", () => doc.setWireConnectionsVisible(connEl.checked));

  // The history and the lock change without a click -- an edit, an undo from a
  // keyboard shortcut -- so the buttons follow the document rather than the
  // clicks on them.
  setInterval(() => syncToolbar(doc), 200);
  syncToolbar(doc);
}

/** Enable and press the toolbar to match what the document will actually do. */
function syncToolbar(doc: Document): void {
  const locked = doc.isLocked();
  $<HTMLButtonElement>("#undo").disabled = !doc.canUndo();
  $<HTMLButtonElement>("#redo").disabled = !doc.canRedo();
  $<HTMLButtonElement>("#cut").disabled = locked || doc.selectedCount() === 0;
  $<HTMLButtonElement>("#copy").disabled = doc.selectedCount() === 0;
  $<HTMLButtonElement>("#paste").disabled = locked;

  const lockBtn = $<HTMLButtonElement>("#lock");
  lockBtn.textContent = locked ? "Locked" : "Lock";
  lockBtn.classList.toggle("locked", locked);
}

function bindFiles(doc: Document): void {
  $<HTMLButtonElement>("#open").addEventListener("click", () => fileEl.click());

  fileEl.addEventListener("change", () => {
    const file = fileEl.files?.[0];
    if (file) void openFile(doc, file);
    // Clear it, or picking the same file twice fires no change event.
    fileEl.value = "";
  });

  $<HTMLButtonElement>("#save").addEventListener("click", () => saveCircuit(doc));

  $<HTMLButtonElement>("#new").addEventListener("click", () => {
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
    const drag = e as DragEvent;

    // A gate dragged out of the palette lands where it was dropped.
    const gate = drag.dataTransfer?.getData("application/x-cedarlogic-gate");
    if (gate) {
      const rect = canvas.getBoundingClientRect();
      const px = Math.round(drag.clientX - rect.left);
      const py = Math.round(drag.clientY - rect.top);
      if (doc.addGate(gate, doc.worldX(px, py), doc.worldY(px, py)) < 0) {
        setStatus(`${gate}: not in the library`, true);
      } else {
        setStatus(`placed ${gate}`);
      }
      return;
    }

    const file = drag.dataTransfer?.files?.[0];
    if (file) void openFile(doc, file);
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

  hintEl.textContent =
    "scroll zooms \u00b7 \u2318-scroll or middle-drag pans \u00b7 space fits \u00b7 delete removes \u00b7 r rotates";

  const palette = new Palette(
    module,
    doc,
    $<HTMLElement>("#gates"),
    $<HTMLSelectElement>("#library"),
    $<HTMLInputElement>("#filter"),
    {
      place(type) {
        // A click drops the gate where you are looking, since a click carries
        // no position of its own.
        const cx = canvas.clientWidth / 2;
        const cy = canvas.clientHeight / 2;
        if (doc.addGate(type, doc.worldX(cx, cy), doc.worldY(cx, cy)) < 0) {
          setStatus(`${type}: not in the library`, true);
          return;
        }
        setStatus(`placed ${type}`);
      },
      beginDrag(type) {
        setStatus(`drop ${type} on the canvas`);
      },
      status: (text) => setStatus(text, true),
    },
  );

  setStatus(`${palette.gateCount} gates \u00b7 open a .cdl or drop one here`);

  // A handle for poking at the engine from the console. Harmless in production
  // and the difference between diagnosing a problem in ten seconds and an hour.
  (window as unknown as { cedar: Document }).cedar = doc;

  // Double-clicking a gate asks the page for an editor; the page names the
  // gate and the shell opens the panel.
  const params = new ParamEditor(doc, $<HTMLElement>("#params"), () =>
    setStatus("parameter set"),
  );
  setInterval(() => params.poll(), 100);
  canvas.addEventListener("pointerdown", () => params.poll());

  bindInput(doc);
  bindFiles(doc);
  bindToolbar(doc);
  bindSimulation(doc);
  startFrameLoop(module, doc);

  // The hint has done its job once the view has been touched.
  const fadeHint = (): void => hintEl.classList.add("faded");
  canvas.addEventListener("wheel", fadeHint, { once: true, passive: true });
  canvas.addEventListener("pointerdown", fadeHint, { once: true });

  requestAnimationFrame(() => doc.zoomAll());
}

void main();
