// Reading wheel events the way the desktop does.
//
// klsGLCanvas::wxOnMouseWheel accumulates raw wheel rotation and only acts once
// it amounts to a whole "line", keeping the remainder for next time. That
// accumulator is why a trackpad feels smooth there: a two-finger drag produces
// a stream of small rotations that build up to one step, instead of each tiny
// movement counting as a full one -- and a jittery reversal spends the pending
// remainder rather than stepping back and forth across the boundary.
//
// One thing does not carry over. wxWidgets hands the desktop rotation already
// scaled to its own units, while a browser reports raw CSS pixels, and a
// trackpad's pixels are far finer than a wheel notch. Measuring both against a
// wheel notch makes a trackpad crawl: a whole gesture would be worth one step.
// So the device is inferred, and it sets the threshold and nothing else --
// a misjudged device changes only how far you scroll per step, never what the
// scroll does.

/** wxWidgets' wheel delta: the rotation one mouse notch reports. */
export const WHEEL_DELTA = 120;

/** A trackpad's travel per step. Chosen so an ordinary drag is a few steps. */
export const TRACKPAD_DELTA = 40;

/** The largest per-event delta a trackpad plausibly reports. */
const TRACKPAD_MAX_DELTA = 40;

/** Just the fields this reads, so it can be driven from a test. */
export interface WheelSample {
  deltaX: number;
  deltaY: number;
  /** 0 = pixels, 1 = lines, 2 = pages. */
  deltaMode: number;
}

export interface WheelSteps {
  x: number;
  y: number;
}

export class WheelReader {
  private rotationX = 0;
  private rotationY = 0;
  private trackpad = false;

  /** What the last events suggest the device is. Exposed for diagnostics. */
  get isTrackpad(): boolean {
    return this.trackpad;
  }

  /**
   * Whole steps this event completed. Positive y is a scroll away from the
   * user, matching wxWidgets' sign, so callers read like the desktop's handler.
   */
  take(e: WheelSample): WheelSteps {
    this.classify(e);

    // Line and page modes report in their own units; one line is one line.
    const scale = e.deltaMode === 0 ? 1 : WHEEL_DELTA;
    this.rotationX -= e.deltaX * scale;
    this.rotationY -= e.deltaY * scale;

    const threshold = this.trackpad ? TRACKPAD_DELTA : WHEEL_DELTA;

    const x = Math.trunc(this.rotationX / threshold);
    const y = Math.trunc(this.rotationY / threshold);
    this.rotationX -= x * threshold;
    this.rotationY -= y * threshold;

    return { x, y };
  }

  /**
   * A trackpad gives itself away by a horizontal component, a fractional delta,
   * or simply a small one. A wheel gives itself away by reporting lines or
   * pages, or by the exact notch. Anything ambiguous keeps the last verdict, so
   * a fast flick -- whose later deltas grow large -- cannot undo what its own
   * first events established.
   */
  private classify(e: WheelSample): void {
    if (e.deltaMode !== 0) {
      this.trackpad = false;
      return;
    }
    if (e.deltaX !== 0 || !Number.isInteger(e.deltaY)) {
      this.trackpad = true;
      return;
    }

    const magnitude = Math.abs(e.deltaY);
    if (magnitude === 0) return;
    if (magnitude % WHEEL_DELTA === 0) {
      this.trackpad = false;
      return;
    }
    if (magnitude < TRACKPAD_MAX_DELTA) this.trackpad = true;
  }
}
