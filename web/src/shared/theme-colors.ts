import type { WireState } from "./constants";
import { WIRE_STATE } from "./constants";

export interface CanvasColors {
  /** Stage / canvas background */
  canvasBg: string;

  /** Grid lines */
  grid: string;

  /** Gate body stroke (unselected) */
  gateStroke: string;
  /** Gate body stroke (selected) */
  gateSelected: string;

  /** Pin dot fill */
  pinDot: string;
  /** Hovered pin highlight */
  pinHover: string;

  /** Toggle button OFF fill */
  toggleOff: string;
  /** Toggle button ON fill */
  toggleOn: string;

  /** Text label fill (LABEL, FROM, TO) */
  labelText: string;

  /** 7-segment display — lit segment */
  sevenSegOn: string;
  /** 7-segment display — unlit segment */
  sevenSegOff: string;
  /** 7-segment display — background */
  sevenSegBg: string;

  /** Wire colors by state */
  wire: Record<WireState, string>;
  /** Wire selected color */
  wireSelected: string;

  /** Selection box fill */
  selectionFill: string;
  /** Selection box / wire preview / paste ghost stroke */
  selectionStroke: string;

  /** Cursor arrow outline */
  cursorOutline: string;
  /** Cursor label text */
  cursorLabelText: string;
  /** Cursor fallback color */
  cursorFallback: string;

  /** Gate preview SVG stroke (toolbar) */
  previewStroke: string;
  /** Gate preview SVG pin fill (toolbar) */
  previewPin: string;

  /** Thumbnail wire color */
  thumbnailWire: string;
  /** Thumbnail gate color */
  thumbnailGate: string;
}

const darkPalette: CanvasColors = {
  canvasBg: "#030712",       // gray-950
  grid: "#1e293b",           // slate-800
  gateStroke: "#e2e8f0",     // slate-200
  gateSelected: "#3b82f6",   // blue-500
  pinDot: "#60a5fa",         // blue-400
  pinHover: "#ef4444",       // red-500
  toggleOff: "#1e293b",      // slate-800
  toggleOn: "#ef4444",       // red-500
  labelText: "#e2e8f0",      // slate-200
  sevenSegOn: "#ff2222",
  sevenSegOff: "#1a0000",
  sevenSegBg: "#0a0a0a",
  wire: {
    [WIRE_STATE.ZERO]: "#AAAAAA",
    [WIRE_STATE.ONE]: "#FF0000",
    [WIRE_STATE.HI_Z]: "#00C700",
    [WIRE_STATE.UNKNOWN]: "#4D4DFF",
    [WIRE_STATE.CONFLICT]: "#00FFFF",
  },
  wireSelected: "#3b82f6",
  selectionFill: "rgba(59, 130, 246, 0.1)",
  selectionStroke: "#3b82f6",
  cursorOutline: "#000",
  cursorLabelText: "#fff",
  cursorFallback: "#888",
  previewStroke: "#94a3b8",  // slate-400
  previewPin: "#60a5fa",     // blue-400
  thumbnailWire: "#555",
  thumbnailGate: "#888",
};

const lightPalette: CanvasColors = {
  canvasBg: "#ffffff",
  grid: "#e2e8f0",           // slate-200
  gateStroke: "#334155",     // slate-700
  gateSelected: "#2563eb",   // blue-600
  pinDot: "#3b82f6",         // blue-500
  pinHover: "#dc2626",       // red-600
  toggleOff: "#cbd5e1",      // slate-300
  toggleOn: "#dc2626",       // red-600
  labelText: "#334155",      // slate-700
  sevenSegOn: "#dc2626",
  sevenSegOff: "#fecaca",    // red-100
  sevenSegBg: "#f1f5f9",     // slate-100
  wire: {
    [WIRE_STATE.ZERO]: "#64748b",  // slate-500
    [WIRE_STATE.ONE]: "#dc2626",   // red-600
    [WIRE_STATE.HI_Z]: "#16a34a",  // green-600
    [WIRE_STATE.UNKNOWN]: "#2563eb", // blue-600
    [WIRE_STATE.CONFLICT]: "#0891b2", // cyan-600
  },
  wireSelected: "#2563eb",
  selectionFill: "rgba(37, 99, 235, 0.1)",
  selectionStroke: "#2563eb",
  cursorOutline: "#fff",
  cursorLabelText: "#fff",
  cursorFallback: "#888",
  previewStroke: "#475569",  // slate-600
  previewPin: "#3b82f6",     // blue-500
  thumbnailWire: "#94a3b8",  // slate-400
  thumbnailGate: "#64748b",  // slate-500
};

export function getCanvasColors(theme: "light" | "dark"): CanvasColors {
  return theme === "dark" ? darkPalette : lightPalette;
}
