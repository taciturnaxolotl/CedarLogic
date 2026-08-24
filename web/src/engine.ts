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
