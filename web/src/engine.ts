// Loading and typing the wasm engine.
//
// The module is emitted by wasm/gui/build.sh with EXPORT_ES6, so it arrives as
// a default-exported factory returning a promise. Emscripten does not generate
// types for embind classes, so the shapes the shell actually uses are declared
// here by hand -- they mirror the EMSCRIPTEN_BINDINGS block in
// wasm/gui/bindings.cpp and must move with it.

/** An embind std::vector, which is indexed rather than iterated. */
export interface VectorLike<T> {
  size(): number;
  get(index: number): T;
  delete(): void;
}

export interface Document {
  /** Empty when the gate library parsed; a message when it did not. */
  loadError(): string;
  gateTypes(): VectorLike<string>;
  /** Returns the new gate's id, or -1 if the library has no such type. */
  addGate(type: string, x: number, y: number): number;
  /** The page background the render style asks for, as a CSS colour. */
  background(): string;
  /** Read a .cdl. Returns "" on success, or a message. Migrates legacy files. */
  loadCircuit(text: string): string;
  /** The circuit as v3 .cdl text. */
  saveCircuit(): string;
  /** Anything the last load wanted to say. Silence means a clean read. */
  loadNotices(): VectorLike<string>;
  clearCircuit(): void;
  gateCount(): number;
  wireCount(): number;
  // Input. Positions are CSS pixels relative to the canvas; `button` follows
  // the DOM (0 left, 1 middle, 2 right). These reach the same handlers the
  // desktop runs, so selection, dragging and the click-vs-drag dead zone
  // behave identically.
  pointerDown(px: number, py: number, button: number,
              shift: boolean, ctrl: boolean, alt: boolean, meta: boolean): void;
  pointerMove(px: number, py: number, leftDown: boolean,
              shift: boolean, ctrl: boolean, alt: boolean, meta: boolean): void;
  pointerUp(px: number, py: number, button: number, doubleClick: boolean,
            shift: boolean, ctrl: boolean, alt: boolean, meta: boolean): void;
  /** `key` is a DOM KeyboardEvent.key value. */
  keyDown(key: string, shift: boolean, ctrl: boolean, alt: boolean, meta: boolean): void;

  /** Advance the circuit by `elapsedMs` of real time. Call once per frame. */
  stepSimulation(elapsedMs: number): void;
  /** One time step, regardless of wall time. */
  stepOnce(): void;
  isSimulating(): boolean;
  setSimulating(on: boolean): void;
  /** The core stopped because the circuit could not keep up. */
  inPanic(): boolean;
  clearPanic(): void;

  deleteSelection(): void;
  rotateSelection(): void;
  copySelection(): void;
  cutSelection(): void;
  paste(): void;
  clipboardText(): string;
  setClipboardText(text: string): void;

  undo(): boolean;
  redo(): boolean;
  canUndo(): boolean;
  canRedo(): boolean;
  selectedCount(): number;
  /** True once, if the page asked the shell to open a gate picker. */
  takeQuickAddRequest(): boolean;

  /** Tell the camera how big the drawing surface is, in CSS pixels. */
  setViewportSize(width: number, height: number): void;
  getZoom(): number;
  panX(): number;
  panY(): number;
  translatePan(dx: number, dy: number): void;
  /** Zoom by wheel notches about a CSS-pixel point, keeping it under the cursor. */
  zoomAt(notches: number, px: number, py: number): void;
  /** Fit the whole circuit to the view. */
  zoomAll(): void;
  worldX(px: number, py: number): number;
  worldY(px: number, py: number): number;
  /** Whether anything changed since the last render. */
  isDirty(): boolean;
  /** Record a frame at the live camera. */
  render(contentScale: number): void;
  /** Byte offset of the recorded stream in wasm memory. Valid until next render. */
  sceneData(): number;
  /** Length of the stream in floats. */
  sceneLength(): number;
  sceneStrings(): VectorLike<string>;
  delete(): void;
}

export interface EngineModule {
  HEAPF32: Float32Array;
  Document: { new (): Document };
}

type Factory = () => Promise<EngineModule>;

// The engine is served from public/, not bundled: it is a build artifact of the
// C++ side, and its .wasm must sit beside its .js at a stable URL for
// emscripten's own loader to find. Vite refuses to transform anything in
// public/, so the specifier is assembled from window.location at call time --
// a literal, however indirect, gets constant-folded and resolved at build time.
const ENGINE_PATH = "/engine/cedarlogic-gui.js";

export async function loadEngine(): Promise<EngineModule> {
  const url = new URL(ENGINE_PATH, window.location.href).href;
  const factory = (await import(/* @vite-ignore */ url)) as { default: Factory };
  return factory.default();
}

/** Copy an embind vector into a plain array and release the C++ side. */
export function toArray<T>(vec: VectorLike<T>): T[] {
  const out: T[] = [];
  for (let i = 0; i < vec.size(); i++) out.push(vec.get(i));
  vec.delete();
  return out;
}
