// Reading wheel events, and what a scroll is worth.
//
// klsGLCanvas::wxOnMouseWheel accumulates raw rotation until it amounts to a
// whole 120-unit "line", then zooms one step. That quantisation is fine for a
// mouse, where one notch is one line and there is nothing in between -- but a
// trackpad reports a continuous stream of small movements, and rounding them
// into whole steps is what makes trackpad zoom lurch: you scroll, nothing
// happens, then the view jumps 33% at once.
//
// So the rotation is not rounded. It is converted to a fraction of a step and
// passed straight through, which the camera now accepts. A wheel notch is
// still worth exactly one step, so a mouse behaves exactly as the desktop does;
// a trackpad simply gets the fractions in between, and the view glides.
//
// Nothing here has to guess which device it is talking to, which is the point:
// the same arithmetic serves both.

/** wxWidgets' wheel delta: the rotation one mouse notch reports, and one step. */
export const WHEEL_DELTA = 120;

/** Just the fields this reads, so it can be driven from a test. */
export interface WheelSample {
  deltaX: number;
  deltaY: number;
  /** 0 = pixels, 1 = lines, 2 = pages. */
  deltaMode: number;
}

/** How much scrolling this event is worth, in steps. Positive y scrolls away. */
export interface WheelSteps {
  x: number;
  y: number;
}

/**
 * Convert a wheel event into steps.
 *
 * Positive y is a scroll away from the user, matching wxWidgets' sign so
 * callers read like the desktop's handler. Line and page modes report in their
 * own units, where one reported line is one step.
 */
export function wheelSteps(e: WheelSample): WheelSteps {
  const scale = e.deltaMode === 0 ? 1 / WHEEL_DELTA : 1;
  return { x: -e.deltaX * scale, y: -e.deltaY * scale };
}
