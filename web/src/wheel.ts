// Turning wheel events into whole scroll lines, the way the desktop does.
//
// klsGLCanvas::wxOnMouseWheel accumulates raw wheel rotation and only acts once
// it amounts to a whole "line" (wxWidgets' wheel delta, 120), keeping the
// remainder for next time. That accumulator is why a trackpad feels smooth
// there: a two-finger drag produces a stream of small rotations that build up
// to one step, rather than each tiny movement counting as a full one.
//
// The browser reports the same rotation with the opposite sign and in its own
// units, so this converts and accumulates identically. Keeping it here, as a
// pure function over its own state, means it can be tested against real device
// traces without a DOM.

/** wxWidgets' wheel delta: the rotation that counts as one line. */
export const WHEEL_DELTA = 120;

/** Just the fields the accumulator reads, so it can be driven from a test. */
export interface WheelSample {
  deltaX: number;
  deltaY: number;
  /** 0 = pixels, 1 = lines, 2 = pages. */
  deltaMode: number;
}

export interface WheelLines {
  x: number;
  y: number;
}

/**
 * Accumulates wheel rotation and hands back whole lines.
 *
 * Positive y is a scroll away from the user, matching wxWidgets' sign so the
 * arithmetic downstream reads like the desktop's. The DOM's deltas run the
 * other way, so they are negated on the way in.
 */
export class WheelAccumulator {
  private rotationX = 0;
  private rotationY = 0;

  /** Whole lines this event completed. Often {x: 0, y: 0} on a trackpad. */
  take(e: WheelSample): WheelLines {
    // Line and page modes report in their own units; one line is one line.
    const scale = e.deltaMode === 0 ? 1 : WHEEL_DELTA;

    this.rotationX -= e.deltaX * scale;
    this.rotationY -= e.deltaY * scale;

    const x = Math.trunc(this.rotationX / WHEEL_DELTA);
    const y = Math.trunc(this.rotationY / WHEEL_DELTA);
    this.rotationX -= x * WHEEL_DELTA;
    this.rotationY -= y * WHEEL_DELTA;

    return { x, y };
  }
}
