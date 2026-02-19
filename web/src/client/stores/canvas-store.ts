import { create } from "zustand";

export interface WireDrawingState {
  fromGateId: string;
  fromPin: string;
  segments: Array<{ x1: number; y1: number; x2: number; y2: number }>;
  currentX: number;
  currentY: number;
}

interface CanvasState {
  viewportX: number;
  viewportY: number;
  zoom: number;

  // Selection as a plain object so Zustand's shallow compare detects changes
  selectedIds: Record<string, true>;
  selectionBox: { x: number; y: number; width: number; height: number } | null;

  wireDrawing: WireDrawingState | null;

  setViewport: (x: number, y: number, zoom: number) => void;
  select: (id: string) => void;
  selectOnly: (id: string) => void;
  toggleSelection: (id: string) => void;
  clearSelection: () => void;
  isSelected: (id: string) => boolean;
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

  isSelected: (id) => !!get().selectedIds[id],

  setSelectionBox: (selectionBox) => set({ selectionBox }),
  setWireDrawing: (wireDrawing) => set({ wireDrawing }),
}));
