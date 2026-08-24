// The web shell: load the engine, place gates, drive the camera, draw.
//
// Everything visible on the canvas was drawn by the same C++ that draws the
// desktop, and every pan and zoom went through the same CanvasCamera. This file
// supplies a surface, a frame loop, and a translation from DOM events -- it
// does not decide how a gate looks or how far a wheel notch zooms.

import { loadEngine, toArray, type Document, type EngineModule } from "./engine.ts";
import { replay } from "./scene.ts";

const canvas = document.querySelector<HTMLCanvasElement>("#canvas")!;
const statusEl = document.querySelector<HTMLDivElement>("#status")!;
const gatesEl = document.querySelector<HTMLUListElement>("#gates")!;

function setStatus(text: string, isError = false): void {
  statusEl.textContent = text;
  statusEl.classList.toggle("error", isError);
}

/** Draws whenever the document says something changed, and no more often. */
function startFrameLoop(module: EngineModule, doc: Document): () => void {
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
      // business, passed to render() below.
      doc.setViewportSize(width, height);
    }

    if (doc.isDirty() && width > 0 && height > 0) {
      doc.render(ratio);

      const cmds = new Float32Array(module.HEAPF32.buffer, doc.sceneData(), doc.sceneLength());
      const strings = toArray(doc.sceneStrings());

      ctx.fillStyle = doc.background();
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      // The stream is already in device pixels (render() was given the ratio),
      // so the replayer must not scale it again.
      replay(ctx, cmds, strings, { pixelRatio: 1, font: "ui-monospace, monospace" });
    }

    requestAnimationFrame(frame);
  };

  requestAnimationFrame(frame);
  return () => {};
}

/** Translate DOM input into camera moves. */
function bindCamera(doc: Document): void {
  // A wheel notch is one zoom step, pivoting on the cursor -- the desktop's
  // zoomToMouse, reached through the same camera.
  canvas.addEventListener(
    "wheel",
    (e) => {
      e.preventDefault();
      const rect = canvas.getBoundingClientRect();
      // Trackpads report small deltas and mice report large ones; take the sign
      // so one gesture is one step either way.
      const notches = e.deltaY < 0 ? 1 : -1;
      doc.zoomAt(notches, Math.round(e.clientX - rect.left), Math.round(e.clientY - rect.top));
    },
    { passive: false },
  );

  // Middle-drag pans, as on the desktop. Space-drag does too, because a browser
  // canvas cannot rely on a middle button being there.
  let panning = false;
  let lastX = 0;
  let lastY = 0;
  let spaceHeld = false;

  canvas.addEventListener("pointerdown", (e) => {
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
    canvas.releasePointerCapture(e.pointerId);
  };
  canvas.addEventListener("pointerup", endPan);
  canvas.addEventListener("pointercancel", endPan);

  window.addEventListener("keydown", (e) => {
    if (e.code === "Space") {
      spaceHeld = true;
      // Space alone fits the circuit, matching the desktop.
      if (!panning) {
        e.preventDefault();
        doc.zoomAll();
      }
    }
  });
  window.addEventListener("keyup", (e) => {
    if (e.code === "Space") spaceHeld = false;
  });
}

async function main(): Promise<void> {
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
  setStatus(`${types.length} gate types loaded\nwheel to zoom, space to fit`);

  // Something on screen at startup, so the page is evidence rather than a blank
  // canvas waiting to be clicked.
  types.slice(0, 6).forEach((type, index) => {
    doc.addGate(type, (index % 3) * 10, Math.floor(index / 3) * -8);
  });

  for (const type of types) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.textContent = type;
    button.addEventListener("click", () => {
      // Place into the middle of whatever the camera is looking at.
      const x = doc.worldX(Math.round(canvas.clientWidth / 2), Math.round(canvas.clientHeight / 2));
      const y = doc.worldY(Math.round(canvas.clientWidth / 2), Math.round(canvas.clientHeight / 2));
      if (doc.addGate(type, x, y) < 0) {
        setStatus(`${type}: not in the library`, true);
        return;
      }
      setStatus(`placed ${type}`);
    });
    item.append(button);
    gatesEl.append(item);
  }

  bindCamera(doc);
  startFrameLoop(module, doc);

  // Wait for the first frame to size the viewport, then fit.
  requestAnimationFrame(() => doc.zoomAll());
}

void main();
