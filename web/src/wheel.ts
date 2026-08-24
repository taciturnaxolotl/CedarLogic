// Telling a mouse wheel from a trackpad.
//
// Both arrive as `wheel` events and they want opposite things: a wheel notch
// should zoom, as the desktop does, while a trackpad's two-finger drag should
// pan, as every other canvas application does. The event does not say which
// device produced it, so it has to be inferred -- and the inference has to be
// *remembered*, because deciding afresh per event is what makes a diagonal
// trackpad drag alternate between panning and zooming several times a second.
//
// So this is a small state machine over the evidence, not a threshold.

/** The canonical delta a wheel notch reports in pixel mode. */
const WHEEL_NOTCH = 120;

/** A trackpad's per-event travel. A wheel notch is never this small. */
const TRACKPAD_MAX_DELTA = 40;

/** Just the fields the decision reads, so it can be tested without a DOM. */
export interface WheelSample {
  deltaX: number;
  deltaY: number;
  /** 0 = pixels, 1 = lines, 2 = pages. */
  deltaMode: number;
}

/**
 * Update `wasTrackpad` with whatever this event reveals about the device.
 *
 * A trackpad gives itself away three ways: a horizontal component, a fractional
 * delta, or simply a small one. A wheel gives itself away by reporting lines or
 * pages rather than pixels, or by the exact 120-unit notch. Anything ambiguous
 * returns the previous verdict rather than guessing again -- which is what
 * keeps a fast trackpad flick, whose deltas can grow large, from undoing the
 * verdict its own first events established.
 */
export function looksLikeTrackpad(e: WheelSample, wasTrackpad: boolean): boolean {
  // Lines or pages: no trackpad reports those.
  if (e.deltaMode !== 0) return false;

  if (e.deltaX !== 0 || !Number.isInteger(e.deltaY)) return true;

  const magnitude = Math.abs(e.deltaY);
  if (magnitude === 0) return wasTrackpad;

  if (magnitude % WHEEL_NOTCH === 0) return false;
  if (magnitude < TRACKPAD_MAX_DELTA) return true;

  return wasTrackpad;
}
