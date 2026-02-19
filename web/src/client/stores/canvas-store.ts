import { create } from "zustand";

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

  setViewport: (x: number, y: number, zoom: number) => void;
  selectOnly: (id: string) => void;
  select: (id: string) => void;
  toggleSelection: (id: string) => void;
  clearSelection: () => void;
  setSelectionBox: (box: CanvasState["selectionBox"]) => void;
  setWireDrawing: (state: WireDrawingState | null) => void;
}

export const useCanvasStore = create<CanvasState>((set, get) => ({
  viewportX: 0,
  viewportY: 0,
  zoom: 1,
  selectedIds: {},
  selectionBox: null,
  wireDrawing: null,

  setViewport: (viewportX, viewportY, zoom) => set({ viewportX, viewportY, zoom }),

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
}));
