// Traces from real devices, so the classifier is judged on what browsers
// actually report rather than on what the spec permits.
import { looksLikeTrackpad } from "./wheel.ts";
import assert from "node:assert/strict";

const run = (events) => events.reduce((verdict, e) =>
  looksLikeTrackpad({ deltaX: 0, deltaMode: 0, ...e }, verdict), false);

const cases = [
  // The one that was broken: a straight two-finger drag has no horizontal
  // component at all, and Chrome rounds its deltas to integers.
  ["trackpad, straight down", [-2, -6, -11, -14, -12, -7, -3].map((deltaY) => ({ deltaY })), true],
  ["trackpad, straight up", [3, 9, 15, 13, 6].map((deltaY) => ({ deltaY })), true],
  ["trackpad, diagonal", [{ deltaY: -8, deltaX: 3 }, { deltaY: -11, deltaX: 5 }], true],
  ["trackpad, fractional", [{ deltaY: -4.5 }, { deltaY: -9.25 }], true],
  // A flick's later deltas get large; the verdict must survive them.
  ["trackpad flick", [{ deltaY: -3 }, { deltaY: -20 }, { deltaY: -140 }, { deltaY: -95 }], true],

  ["mouse, one notch", [{ deltaY: 120 }], false],
  ["mouse, several notches", [{ deltaY: -120 }, { deltaY: -120 }, { deltaY: -240 }], false],
  ["mouse, line mode", [{ deltaY: -3, deltaMode: 1 }], false],
  ["mouse, page mode", [{ deltaY: -1, deltaMode: 2 }], false],

  // Plugging a mouse in after using the trackpad must switch back.
  ["trackpad then mouse", [{ deltaY: -6 }, { deltaY: -8 }, { deltaY: 120 }], false],
  ["mouse then trackpad", [{ deltaY: 120 }, { deltaY: -6 }], true],
];

let failed = 0;
for (const [name, events, expected] of cases) {
  const got = run(events);
  const ok = got === expected;
  if (!ok) failed++;
  console.log(`${ok ? "✓" : "✗"} ${name.padEnd(26)} -> ${got ? "trackpad" : "mouse"}${ok ? "" : `  EXPECTED ${expected ? "trackpad" : "mouse"}`}`);
}
assert.equal(failed, 0, `${failed} case(s) failed`);
console.log(`\n${cases.length} cases pass`);
