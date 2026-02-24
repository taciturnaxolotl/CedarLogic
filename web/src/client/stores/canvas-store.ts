import { create } from "zustand";
import type { WireSegment } from "@shared/types";
import type { WireModel } from "@shared/wire-types";

export interface ClipboardData {
  gates: Array<{
    defId: string;
    logicType: string;
    offsetX: number;
    offsetY: number;
    rotation: number;
    params: Record<string, string>;
    originalId: string;
  }>;
  wires: Array<{
    segments?: WireSegment[];
    model?: WireModel;
    originalId: string;
  }>;
  connections: Array<{
    originalGateId: string;
    pinName: string;
    pinDirection: "input" | "output";
    originalWireId: string;
  }>;
}

export interface WireDrawingState {
  fromGateId: string;
  fromPinName: string;
  fromPinDirection: "input" | "output";
  /** Absolute position of the source pin */
  fromX: number;
  fromY: number;
  /** Current mouse position (snapped) */
  currentX: number;
  currentY: number;
}

interface CanvasState {
  viewportX: number;
  viewportY: number;
  zoom: number;

  selectedIds: Record<string, true>;
  selectionBox: { x: number; y: number; width: number; height: number } | null;

  wireDrawing: WireDrawingState | null;

  clipboard: ClipboardData | null;

  /** When non-null, a paste preview is being positioned by the user */
  pendingPaste: { data: ClipboardData; x: number; y: number; shiftKey?: boolean } | null;

  /** When non-null, a gate ghost preview is following the mouse for placement */
  pendingGate: { defId: string; logicType: string; params?: Record<string, string>; x: number; y: number; rotation?: number } | null;

  canvasSize: { width: number; height: number };

  activePage: string;
  pageViewports: Record<string, { x: number; y: number; zoom: number }>;

  hoveredPin: {
    gateId: string;
    pinName: string;
    pinDirection: "input" | "output";
    x: number;
    y: number;
  } | null;

  setViewport: (x: number, y: number, zoom: number) => void;
  setCanvasSize: (width: number, height: number) => void;
  selectOnly: (id: string) => void;
  select: (id: string) => void;
  toggleSelection: (id: string) => void;
  clearSelection: () => void;
  setSelectionBox: (box: CanvasState["selectionBox"]) => void;
  setWireDrawing: (state: WireDrawingState | null) => void;
  setClipboard: (cb: ClipboardData | null) => void;
  setPendingPaste: (pp: CanvasState["pendingPaste"]) => void;
  setPendingGate: (pg: CanvasState["pendingGate"]) => void;
  setHoveredPin: (pin: CanvasState["hoveredPin"]) => void;
  setActivePage: (page: string) => void;
}

export const useCanvasStore = create<CanvasState>((set, get) => ({
  viewportX: 0,
  viewportY: 0,
  zoom: 1,
  selectedIds: {},
  selectionBox: null,
  wireDrawing: null,
  clipboard: null,
  pendingPaste: null,
  pendingGate: null,
  canvasSize: { width: 0, height: 0 },
  activePage: "0",
  pageViewports: {},
  hoveredPin: null,

  setViewport: (viewportX, viewportY, zoom) => set({ viewportX, viewportY, zoom }),
  setCanvasSize: (width, height) => set({ canvasSize: { width, height } }),

  selectOnly: (id) => set({ selectedIds: { [id]: true } }),

  select: (id) =>
    set((s) => ({ selectedIds: { ...s.selectedIds, [id]: true } })),

  toggleSelection: (id) =>
    set((s) => {
      const next = { ...s.selectedIds };
      if (next[id]) {
        delete next[id];
      } else {
        next[id] = true;
      }
      return { selectedIds: next };
    }),

  clearSelection: () => set({ selectedIds: {} }),

  setSelectionBox: (selectionBox) => set({ selectionBox }),
  setWireDrawing: (wireDrawing) => set({ wireDrawing }),
  setClipboard: (clipboard) => set({ clipboard }),
  setPendingPaste: (pendingPaste) => set({ pendingPaste }),
  setPendingGate: (pendingGate) => set({ pendingGate }),
  setHoveredPin: (hoveredPin) => set({ hoveredPin }),

  setActivePage: (page) => {
    const s = get();
    // Save current viewport for current page
    const updated = {
      ...s.pageViewports,
      [s.activePage]: { x: s.viewportX, y: s.viewportY, zoom: s.zoom },
    };
    // Restore target page's viewport (or defaults)
    const target = updated[page] ?? { x: 0, y: 0, zoom: 1 };
    set({
      activePage: page,
      pageViewports: updated,
      viewportX: target.x,
      viewportY: target.y,
      zoom: target.zoom,
      selectedIds: {},
      wireDrawing: null,
      pendingPaste: null,
      pendingGate: null,
    });
  },
}));
