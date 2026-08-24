// The accumulator, against traces from real devices.
import { WheelReader, WHEEL_DELTA, TRACKPAD_DELTA } from "./wheel.ts";
import assert from "node:assert/strict";

const feed = (events) => {
  const acc = new WheelReader();
  let x = 0, y = 0;
  for (const e of events) {
    const lines = acc.take({ deltaX: 0, deltaY: 0, deltaMode: 0, ...e });
    x += lines.x;
    y += lines.y;
  }
  return { x, y };
};

let failed = 0;
const check = (name, got, expected) => {
  const ok = got.x === expected.x && got.y === expected.y;
  if (!ok) failed++;
  console.log(`${ok ? "✓" : "✗"} ${name.padEnd(38)} ${JSON.stringify(got)}${ok ? "" : `  EXPECTED ${JSON.stringify(expected)}`}`);
};

// One mouse notch is one line, and it zooms in (scrolling away is positive).
check("mouse, one notch away", feed([{ deltaY: -WHEEL_DELTA }]), { x: 0, y: 1 });
check("mouse, one notch toward", feed([{ deltaY: WHEEL_DELTA }]), { x: 0, y: -1 });
check("mouse, three notches", feed([{ deltaY: -WHEEL_DELTA }, { deltaY: -WHEEL_DELTA }, { deltaY: -WHEEL_DELTA }]), { x: 0, y: 3 });

// A trackpad's small deltas build up rather than each counting as a step, but
// against a trackpad's threshold: these seven sum to 55, worth one step.
check("trackpad, short drag", feed([-2, -6, -11, -14, -12, -7, -3].map((deltaY) => ({ deltaY }))), { x: 0, y: 1 });

// A gesture worth 360 units is nine steps on a trackpad, not three -- which is
// the difference between responsive and crawling.
const long = Array.from({ length: 30 }, () => ({ deltaY: -12 }));  // 360 total
check("trackpad, long drag", feed(long), { x: 0, y: 360 / TRACKPAD_DELTA });

// Horizontal travel accumulates on its own axis.
check("trackpad, sideways", feed(Array.from({ length: 20 }, () => ({ deltaX: -12 }))), { x: 240 / TRACKPAD_DELTA, y: 0 });

// The remainder is kept, so two half-lines make one line rather than none.
// A mouse's remainder carries too: two half-notches make one step.
check("remainder carries across events", feed([{ deltaY: -70 }, { deltaY: -70 }]), { x: 0, y: 1 });

// Reversing spends the pending remainder before it counts the other way, which
// is what wx does: 70 forward then 140 back leaves -70, still short of a line.
// Without this a jittery finger would step back and forth around a boundary.
check("reversing cancels the remainder", feed([{ deltaY: -70 }, { deltaY: 140 }]), { x: 0, y: 0 });
check("reversing far enough does step", feed([{ deltaY: -70 }, { deltaY: 260 }]), { x: 0, y: -1 });

// The threshold follows the device, and the device follows the evidence.
check("mouse keeps the coarse threshold", feed([{ deltaY: -WHEEL_DELTA }]), { x: 0, y: 1 });

// Line mode: one reported line is one line.
check("mouse, line mode", feed([{ deltaY: -1, deltaMode: 1 }]), { x: 0, y: 1 });
check("mouse, page mode", feed([{ deltaY: -2, deltaMode: 2 }]), { x: 0, y: 2 });

assert.equal(failed, 0, `${failed} case(s) failed`);
console.log("\nall cases pass");
