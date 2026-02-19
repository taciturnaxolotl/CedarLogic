export const WIRE_STATE = {
  ZERO: 0,
  ONE: 1,
  HI_Z: 2,
  CONFLICT: 3,
  UNKNOWN: 4,
} as const;

export type WireState = (typeof WIRE_STATE)[keyof typeof WIRE_STATE];

// Colors matched to desktop CedarLogic (adapted for dark theme)
export const WIRE_COLORS: Record<WireState, string> = {
  [WIRE_STATE.ZERO]:     "#333333",  // Desktop: black — dark grey for visibility on dark bg
  [WIRE_STATE.ONE]:      "#FF0000",  // Desktop: red (1,0,0)
  [WIRE_STATE.HI_Z]:     "#00C700",  // Desktop: green (0, 0.78, 0)
  [WIRE_STATE.UNKNOWN]:  "#4D4DFF",  // Desktop: blue (0.3, 0.3, 1)
  [WIRE_STATE.CONFLICT]: "#00FFFF",  // Desktop: cyan (0, 1, 1)
};

export const GRID_SIZE = 20;
export const SNAP_SIZE = 10;
