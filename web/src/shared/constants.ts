export const WIRE_STATE = {
  ZERO: 0,
  ONE: 1,
  HI_Z: 2,
  CONFLICT: 3,
  UNKNOWN: 4,
} as const;

export type WireState = (typeof WIRE_STATE)[keyof typeof WIRE_STATE];

export const GRID_SIZE = 20;
export const SNAP_SIZE = 10;
