import { create } from "zustand";

interface PanelState {
  leftOpen: boolean;
  toggleLeft: () => void;
  setLeftOpen: (open: boolean) => void;
}

export const usePanelStore = create<PanelState>((set) => ({
  leftOpen: false,
  toggleLeft: () => set((s) => ({ leftOpen: !s.leftOpen })),
  setLeftOpen: (open) => set({ leftOpen: open }),
}));
