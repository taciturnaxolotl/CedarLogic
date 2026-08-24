// The spike shell: load the engine, place a few gates, draw them.
//
// Everything visible on the canvas was drawn by the same C++ that draws the
// desktop -- this file only supplies a size, a context, and a click handler.

import { loadEngine, toArray, type Document, type EngineModule } from "./engine.ts";
import { replay } from "./scene.ts";

const canvas = document.querySelector<HTMLCanvasElement>("#canvas")!;
const statusEl = document.querySelector<HTMLDivElement>("#status")!;
const gatesEl = document.querySelector<HTMLUListElement>("#gates")!;

function setStatus(text: string, isError = false): void {
  statusEl.textContent = text;
  statusEl.classList.toggle("error", isError);
}

function draw(module: EngineModule, doc: Document): void {
  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (width === 0 || height === 0) return;

  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(height * ratio);

  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  // The core fits the circuit to the size it is given, so hand it CSS pixels
  // and let replay() apply the device ratio. Otherwise the fit would be
  // computed for a canvas twice the size the user sees.
  doc.render(width, height);

  const offset = doc.sceneData();
  const length = doc.sceneLength();
  // A view, not a copy: the stream lives in wasm memory and is read once.
  const cmds = new Float32Array(module.HEAPF32.buffer, offset, length);
  const strings = toArray(doc.sceneStrings());

  ctx.fillStyle = doc.background();
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  replay(ctx, cmds, strings, { pixelRatio: ratio, font: "ui-monospace, monospace" });
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
  setStatus(`${types.length} gate types loaded`);

  // Something on screen at startup, so the page is evidence rather than a
  // blank canvas waiting to be clicked.
  types.slice(0, 6).forEach((type, index) => {
    doc.addGate(type, (index % 3) * 10, Math.floor(index / 3) * -8);
  });

  const redraw = () => draw(module, doc);

  for (const type of types) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.textContent = type;
    button.addEventListener("click", () => {
      // Scatter placements so repeated clicks do not stack invisibly.
      const x = (Math.random() - 0.5) * 60;
      const y = (Math.random() - 0.5) * 40;
      if (doc.addGate(type, x, y) < 0) {
        setStatus(`${type}: not in the library`, true);
        return;
      }
      setStatus(`placed ${type}`);
      redraw();
    });
    item.append(button);
    gatesEl.append(item);
  }

  new ResizeObserver(redraw).observe(canvas);
  redraw();
}

void main();
