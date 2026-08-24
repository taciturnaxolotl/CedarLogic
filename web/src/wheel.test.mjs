// What a scroll is worth, against traces from real devices.
import { wheelSteps, WHEEL_DELTA } from "./wheel.ts";
import assert from "node:assert/strict";

const total = (events) =>
  events.reduce((sum, e) => {
    const s = wheelSteps({ deltaX: 0, deltaY: 0, deltaMode: 0, ...e });
    return { x: sum.x + s.x, y: sum.y + s.y };
  }, { x: 0, y: 0 });

let failed = 0;
const close = (a, b) => Math.abs(a - b) < 1e-9;
const check = (name, got, expected) => {
  const ok = close(got.x, expected.x) && close(got.y, expected.y);
  if (!ok) failed++;
  console.log(`${ok ? "✓" : "✗"} ${name.padEnd(40)} ${JSON.stringify({ x: +got.x.toFixed(3), y: +got.y.toFixed(3) })}${ok ? "" : `  EXPECTED ${JSON.stringify(expected)}`}`);
};

// A mouse notch is exactly one step, as on the desktop.
check("mouse, one notch away", total([{ deltaY: -WHEEL_DELTA }]), { x: 0, y: 1 });
check("mouse, one notch toward", total([{ deltaY: WHEEL_DELTA }]), { x: 0, y: -1 });
check("mouse, three notches", total(Array.from({ length: 3 }, () => ({ deltaY: -WHEEL_DELTA }))), { x: 0, y: 3 });

// A trackpad's small movements are worth their fraction rather than nothing.
// These seven sum to 55 units: a bit under half a step, and delivered smoothly
// instead of waiting to jump.
check("trackpad, short drag", total([-2, -6, -11, -14, -12, -7, -3].map((deltaY) => ({ deltaY }))), { x: 0, y: 55 / WHEEL_DELTA });

// The same travel is worth the same zoom whichever device produced it, which is
// what stops a trackpad and a mouse disagreeing.
check("trackpad, one notch worth of travel", total(Array.from({ length: 10 }, () => ({ deltaY: -12 }))), { x: 0, y: 1 });

check("trackpad, sideways", total(Array.from({ length: 10 }, () => ({ deltaX: -12 }))), { x: 1, y: 0 });

// Reversing subtracts rather than stranding a remainder, so a jittery finger
// ends up where it started.
check("reversing returns to where it began", total([{ deltaY: -70 }, { deltaY: 70 }]), { x: 0, y: 0 });

// Line and page modes report their own units.
check("mouse, line mode", total([{ deltaY: -1, deltaMode: 1 }]), { x: 0, y: 1 });
check("mouse, page mode", total([{ deltaY: -2, deltaMode: 2 }]), { x: 0, y: 2 });

assert.equal(failed, 0, `${failed} case(s) failed`);
console.log("\nall cases pass");
