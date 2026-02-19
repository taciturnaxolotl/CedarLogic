import { create } from "zustand";
import type { WireState } from "@shared/constants";

interface SimulationState {
  wireStates: Map<string, WireState>;
  simTime: number;
  running: boolean;
  stepsPerFrame: number;

  updateWireStates: (updates: Array<{ id: string; state: WireState }>) => void;
  setSimTime: (time: number) => void;
  setRunning: (running: boolean) => void;
  setStepsPerFrame: (steps: number) => void;
}

export const useSimulationStore = create<SimulationState>((set) => ({
  wireStates: new Map(),
  simTime: 0,
  running: true,
  stepsPerFrame: 5,

  updateWireStates: (updates) =>
    set((s) => {
      const next = new Map(s.wireStates);
      for (const u of updates) next.set(u.id, u.state);
      return { wireStates: next };
    }),
  setSimTime: (simTime) => set({ simTime }),
  setRunning: (running) => set({ running }),
  setStepsPerFrame: (stepsPerFrame) => set({ stepsPerFrame }),
}));
