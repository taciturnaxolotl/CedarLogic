import { create } from "zustand";
import { useSyncExternalStore } from "react";
import type { WireState } from "@shared/constants";
import { WIRE_STATE } from "@shared/constants";

// ---------------------------------------------------------------------------
// Zustand store — only holds non-per-wire simulation state
// ---------------------------------------------------------------------------

interface SimulationState {
  simTime: number;
  running: boolean;
  stepsPerFrame: number;

  setSimTime: (time: number) => void;
  setRunning: (running: boolean) => void;
  setStepsPerFrame: (steps: number) => void;
}

export const useSimulationStore = create<SimulationState>((set) => ({
  simTime: 0,
  running: true,
  stepsPerFrame: 16,

  setSimTime: (simTime) => set({ simTime }),
  setRunning: (running) => set({ running }),
  setStepsPerFrame: (stepsPerFrame) => set({ stepsPerFrame }),
}));

// ---------------------------------------------------------------------------
// External mutable wire-state store (no cloning, fine-grained subscriptions)
// ---------------------------------------------------------------------------

type WireListener = () => void;

const wireStates = new Map<string, WireState>();
const wireListeners = new Map<string, Set<WireListener>>();

/** Bulk-update wire states. Only notifies listeners for wires that actually changed. */
export function updateWireStates(updates: Array<{ id: string; state: WireState }>) {
  for (const { id, state } of updates) {
    const prev = wireStates.get(id);
    if (prev === state) continue;
    wireStates.set(id, state);
    const listeners = wireListeners.get(id);
    if (listeners) {
      for (const fn of listeners) fn();
    }
  }
}

/** Read wire state directly (non-reactive). */
export function getWireState(wireId: string): WireState {
  return wireStates.get(wireId) ?? WIRE_STATE.UNKNOWN;
}

function subscribeWire(wireId: string, listener: WireListener): () => void {
  let set = wireListeners.get(wireId);
  if (!set) {
    set = new Set();
    wireListeners.set(wireId, set);
  }
  set.add(listener);
  return () => {
    set!.delete(listener);
    if (set!.size === 0) wireListeners.delete(wireId);
  };
}

/**
 * Subscribe to a single wire's state. Only re-renders when this wire changes.
 */
export function useWireState(wireId: string): WireState {
  return useSyncExternalStore(
    (cb) => subscribeWire(wireId, cb),
    () => wireStates.get(wireId) ?? WIRE_STATE.UNKNOWN,
  );
}

/**
 * Subscribe to multiple wires (e.g. REGISTER inputs). Re-renders when any of them change.
 * Returns a function to look up state by wireId (avoids allocating a new Map each render).
 */
export function useWireStates(wireIds: string[]): (wireId: string) => WireState {
  useSyncExternalStore(
    (cb) => {
      const unsubs = wireIds.map((id) => subscribeWire(id, cb));
      return () => { for (const u of unsubs) u(); };
    },
    () => {
      // Snapshot identity — we need a stable value when nothing changed.
      // useSyncExternalStore compares with Object.is, so we return a number
      // that changes whenever any subscribed wire changes.
      let h = 0;
      for (const id of wireIds) {
        h = (h * 5 + (wireStates.get(id) ?? WIRE_STATE.UNKNOWN)) | 0;
      }
      return h;
    },
  );
  return (id: string) => wireStates.get(id) ?? WIRE_STATE.UNKNOWN;
}
