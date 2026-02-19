export const WIRE_STATE = {
  ZERO: 0,
  ONE: 1,
  HI_Z: 2,
  CONFLICT: 3,
  UNKNOWN: 4,
} as const;

export type WireState = (typeof WIRE_STATE)[keyof typeof WIRE_STATE];

export const WIRE_COLORS: Record<WireState, string> = {
  [WIRE_STATE.ZERO]: "#00CC00",
  [WIRE_STATE.ONE]: "#FF0000",
  [WIRE_STATE.HI_Z]: "#0066FF",
  [WIRE_STATE.CONFLICT]: "#FF00FF",
  [WIRE_STATE.UNKNOWN]: "#888888",
};

export const GRID_SIZE = 20;
export const SNAP_SIZE = 10;
