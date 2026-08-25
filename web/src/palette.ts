// The gate palette.
//
// The desktop shows every gate in a library as a small picture of itself,
// grouped into sections you choose between, and you drag one onto the canvas.
// This is that, and the pictures are not approximations: each tile is recorded
// by the engine through the same drawToScene the canvas uses, framed by the
// same thumbnailTransform the desktop's palette uses, and replayed by the same
// scene replayer. A tile and the gate it places cannot look different.

import type { Document, EngineModule } from "./engine.ts";
import { toArray } from "./engine.ts";
import { replay } from "./scene.ts";

/** Tile art size in CSS pixels; the element is sized by the stylesheet. */
const TILE = 48;

export interface PaletteCallbacks {
  /** Place this gate at the middle of the view (a click on a tile). */
  place(type: string): void;
  /** Begin dragging this gate onto the canvas. */
  beginDrag(type: string): void;
  status(text: string): void;
}

export class Palette {
  private readonly libraries: string[];
  private readonly gatesByLibrary = new Map<string, string[]>();
  private readonly libraryOfGate = new Map<string, string>();

  // Tiles draw when they scroll into view rather than all at once. A library of
  // thirty gates is thirty engine renders and thirty canvas replays, and doing
  // them during startup delays the first paint of everything else -- including
  // the circuit, which is what the user came for.
  private readonly pending = new WeakMap<Element, () => void>();
  private readonly observer: IntersectionObserver;

  constructor(
    private readonly module: EngineModule,
    private readonly doc: Document,
    private readonly root: HTMLElement,
    private readonly chooser: HTMLSelectElement,
    private readonly filter: HTMLInputElement,
    private readonly callbacks: PaletteCallbacks,
  ) {
    this.observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (!entry.isIntersecting) continue;
          const draw = this.pending.get(entry.target);
          if (!draw) continue;
          this.pending.delete(entry.target);
          this.observer.unobserve(entry.target);
          draw();
        }
      },
      // A little ahead of the viewport, so a tile is drawn by the time it
      // arrives rather than appearing blank and filling in.
      { root: root, rootMargin: "150px" },
    );

    this.libraries = toArray(doc.libraryNames());
    for (const library of this.libraries) {
      const gates = toArray(doc.gatesInLibrary(library));
      this.gatesByLibrary.set(library, gates);
      for (const gate of gates) this.libraryOfGate.set(gate, library);
    }

    for (const library of this.libraries) {
      const option = document.createElement("option");
      option.value = library;
      option.textContent = library;
      chooser.append(option);
    }

    chooser.addEventListener("change", () => {
      // Choosing a section is a different question from searching all of them.
      filter.value = "";
      this.render();
    });
    filter.addEventListener("input", () => this.render());

    this.render();
  }

  /** How many gates the library knows, for the status line. */
  get gateCount(): number {
    return this.libraryOfGate.size;
  }

  private render(): void {
    const query = this.filter.value.trim().toLowerCase();
    // Tiles from the previous view are gone; stop waiting on them.
    this.observer.disconnect();
    this.root.replaceChildren();

    if (query === "") {
      const library = this.chooser.value || this.libraries[0];
      for (const gate of this.gatesByLibrary.get(library) ?? []) {
        this.root.append(this.tile(gate));
      }
      return;
    }

    // A search crosses libraries, so it needs to say which one each match came
    // from -- otherwise two similarly named gates are indistinguishable.
    let matches = 0;
    for (const library of this.libraries) {
      const hits = (this.gatesByLibrary.get(library) ?? []).filter((g) =>
        g.toLowerCase().includes(query),
      );
      if (hits.length === 0) continue;

      const heading = document.createElement("div");
      heading.className = "section";
      heading.textContent = library;
      this.root.append(heading, ...hits.map((g) => this.tile(g)));
      matches += hits.length;
    }

    if (matches === 0) {
      const empty = document.createElement("div");
      empty.className = "section";
      empty.textContent = `nothing matches “${this.filter.value.trim()}”`;
      this.root.append(empty);
    }
  }

  private tile(type: string): HTMLElement {
    const tile = document.createElement("button");
    tile.type = "button";
    tile.className = "tile";
    tile.title = type;
    tile.draggable = true;

    const canvas = document.createElement("canvas");
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(TILE * ratio);
    canvas.height = Math.round(TILE * ratio);

    const label = document.createElement("span");
    // The library prefix orders the list and is noise once you can see the
    // gate; the name after it is what anyone calls the thing.
    label.textContent = type.replace(/^[A-Z]{2}_/, "");

    tile.append(canvas, label);
    this.pending.set(tile, () => this.draw(canvas, type, Math.round(TILE * ratio)));
    this.observer.observe(tile);

    tile.addEventListener("click", () => this.callbacks.place(type));
    tile.addEventListener("dragstart", (e) => {
      tile.classList.add("dragging");
      e.dataTransfer?.setData("application/x-cedarlogic-gate", type);
      if (e.dataTransfer) e.dataTransfer.effectAllowed = "copy";
      this.callbacks.beginDrag(type);
    });
    tile.addEventListener("dragend", () => tile.classList.remove("dragging"));

    return tile;
  }

  /** Record the gate through the engine and replay it into the tile. */
  private draw(canvas: HTMLCanvasElement, type: string, sizePx: number): void {
    if (!this.doc.renderGateThumbnail(type, sizePx)) {
      this.callbacks.status(`${type}: could not be drawn`);
      return;
    }
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const cmds = new Float32Array(
      this.module.HEAPF32.buffer,
      this.doc.sceneData(),
      this.doc.sceneLength(),
    );
    const strings = toArray(this.doc.sceneStrings());

    ctx.fillStyle = this.doc.background();
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    replay(ctx, cmds, strings, { pixelRatio: 1, font: "ui-monospace, monospace" });
  }
}
