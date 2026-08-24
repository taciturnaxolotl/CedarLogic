// Replay a recorded CedarLogic frame onto a Canvas2D context.
//
// This is the other half of wasm/gui/SceneBuffer.h. The core records a frame as
// one packed float32 stream and hands it over as a view into wasm memory; this
// walks it once and issues the matching canvas calls. The two files describe the
// same wire format and have to move together.
//
// Nothing here decides how the circuit looks -- every colour, width and
// coordinate was chosen by the same C++ that draws the desktop. This is a
// transcription, and it should stay one.
//
// Two things it does decide, both for speed, because a large schematic is
// thousands of primitives and a naive transcription spends a whole frame budget
// on draw-call overhead:
//
//   The canvas transform is left at identity and points are transformed here.
//   Canvas2D has no retained transform stack worth using when the stream
//   already carries one, and working in device space means a stroke width is
//   just a width -- no dividing by a scale, and no getTransform() per stroke,
//   which allocates a DOMMatrix every time.
//
//   Consecutive primitives that share a style are collected into one path and
//   drawn with one call. Only *consecutive* ones, so draw order is exactly
//   preserved: a fill between two strokes still lands between them.

export const enum Op {
  SetViewport = 0,
  PushTransform = 1,
  PopTransform = 2,
  Lines = 3,
  Polyline = 4,
  FillPolygon = 5,
  FillCircle = 6,
  StrokeCircle = 7,
  FillRect = 8,
  Text = 9,
  Arc = 10,
}

/** Scene::Cap, in the order the enum declares it. */
const CAPS: CanvasLineCap[] = ["butt", "round", "square"];

/** What Scene::Stroke marks as `dashed`: the selection stipple. */
const DASH_PATTERN = [4, 4];

/** Reused, so clearing the dash does not allocate. */
const EMPTY_DASH: number[] = [];

const DEG = Math.PI / 180;

/**
 * How many straight pieces an arc needs to read as a curve. Scaled by how big
 * it is on screen and how far it sweeps, so a tiny inversion bubble does not
 * cost the same as a gate body spanning the view.
 */
function arcSegments(devicePixelRadius: number, sweepDeg: number): number {
  const span = Math.abs(sweepDeg) / 360;
  const ideal = Math.ceil(Math.sqrt(Math.max(devicePixelRadius, 1)) * 4 * span);
  return Math.min(64, Math.max(4, ideal));
}

export interface ReplayOptions {
  /** CSS font family for text commands. Must match what measured the layout. */
  font?: string;
  /** Device pixel ratio, so a frame recorded in device pixels lands crisp. */
  pixelRatio?: number;
}

/**
 * Draw one recorded frame. `cmds` is the float stream, `strings` the text table
 * its Text commands index into.
 */
export function replay(
  ctx: CanvasRenderingContext2D,
  cmds: Float32Array,
  strings: readonly string[],
  options: ReplayOptions = {},
): void {
  const font = options.font ?? "sans-serif";
  const ratio = options.pixelRatio ?? 1;

  let i = 0;

  // The composed transform, as [a b c d e f], and the stack it is pushed onto.
  // Starts as the device-ratio scale; the viewport command replaces the rest.
  let a = ratio, b = 0, c = 0, d = ratio, e = 0, f = 0;
  const stack: number[] = [];

  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.setLineDash(EMPTY_DASH);
  let dashed = false;

  // The batch in progress: what kind of drawing it is, and the style key that
  // consecutive primitives must match to join it.
  const enum Batch { None, Stroke, Fill }
  let batch = Batch.None;
  let batchKey = "";

  const flush = (): void => {
    if (batch === Batch.Stroke) ctx.stroke();
    else if (batch === Batch.Fill) ctx.fill();
    batch = Batch.None;
  };

  const readColor = (): string => {
    const r = (cmds[i++] * 255) | 0;
    const g = (cmds[i++] * 255) | 0;
    const bl = (cmds[i++] * 255) | 0;
    const al = cmds[i++];
    return `rgba(${r},${g},${bl},${al})`;
  };

  const setDashed = (want: boolean): void => {
    if (want === dashed) return;
    ctx.setLineDash(want ? DASH_PATTERN : EMPTY_DASH);
    dashed = want;
  };

  /** Begin or continue a stroke batch, reading the stroke record as it goes. */
  const beginStroke = (): void => {
    const color = readColor();
    const width = cmds[i++];
    const cap = cmds[i++];
    const wantDashed = cmds[i++] !== 0;
    const key = `${color}|${width}|${cap}|${wantDashed}`;

    if (batch === Batch.Stroke && key === batchKey) return;
    flush();
    setDashed(wantDashed);
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.lineCap = CAPS[cap] ?? "butt";
    ctx.beginPath();
    batch = Batch.Stroke;
    batchKey = key;
  };

  const beginFill = (): void => {
    const color = readColor();
    if (batch === Batch.Fill && color === batchKey) return;
    flush();
    ctx.fillStyle = color;
    ctx.beginPath();
    batch = Batch.Fill;
    batchKey = color;
  };

  // Transform a point into device space. Written out rather than via a matrix
  // object because it runs once per vertex, tens of thousands of times a frame.
  const tx = (x: number, y: number): number => a * x + c * y + e;
  const ty = (x: number, y: number): number => b * x + d * y + f;

  const tracePoints = (count: number, closed: boolean): void => {
    ctx.moveTo(tx(cmds[i], cmds[i + 1]), ty(cmds[i], cmds[i + 1]));
    for (let p = 1; p < count; p++) {
      const x = cmds[i + p * 2], y = cmds[i + p * 2 + 1];
      ctx.lineTo(tx(x, y), ty(x, y));
    }
    i += count * 2;
    if (closed) ctx.closePath();
  };

  /** Uniform scale of the current transform, for radii. */
  const scale = (): number => Math.hypot(a, b) || 1;

  while (i < cmds.length) {
    const op = cmds[i++];

    switch (op) {
      case Op.SetViewport: {
        flush();
        // A new viewport replaces the old one rather than composing with it,
        // then the device ratio scales the result.
        a = cmds[i] * ratio; b = cmds[i + 1] * ratio;
        c = cmds[i + 2] * ratio; d = cmds[i + 3] * ratio;
        e = cmds[i + 4] * ratio; f = cmds[i + 5] * ratio;
        stack.length = 0;
        i += 6;
        break;
      }

      case Op.PushTransform: {
        stack.push(a, b, c, d, e, f);
        // Compose local onto current: the gate's model matrix under the camera.
        const la = cmds[i], lb = cmds[i + 1], lc = cmds[i + 2];
        const ld = cmds[i + 3], le = cmds[i + 4], lf = cmds[i + 5];
        i += 6;
        const na = a * la + c * lb;
        const nb = b * la + d * lb;
        const nc = a * lc + c * ld;
        const nd = b * lc + d * ld;
        const ne = a * le + c * lf + e;
        const nf = b * le + d * lf + f;
        a = na; b = nb; c = nc; d = nd; e = ne; f = nf;
        break;
      }

      case Op.PopTransform: {
        f = stack.pop()!; e = stack.pop()!; d = stack.pop()!;
        c = stack.pop()!; b = stack.pop()!; a = stack.pop()!;
        break;
      }

      case Op.Lines: {
        beginStroke();
        // Disconnected pairs: p0-p1, p2-p3, ...
        const count = cmds[i++];
        for (let p = 0; p + 1 < count; p += 2) {
          const x0 = cmds[i + p * 2], y0 = cmds[i + p * 2 + 1];
          const x1 = cmds[i + (p + 1) * 2], y1 = cmds[i + (p + 1) * 2 + 1];
          ctx.moveTo(tx(x0, y0), ty(x0, y0));
          ctx.lineTo(tx(x1, y1), ty(x1, y1));
        }
        i += count * 2;
        break;
      }

      case Op.Polyline: {
        beginStroke();
        const closed = cmds[i++] !== 0;
        tracePoints(cmds[i++], closed);
        break;
      }

      case Op.FillPolygon: {
        beginFill();
        tracePoints(cmds[i++], true);
        break;
      }

      case Op.FillCircle: {
        beginFill();
        const cx = cmds[i++], cy = cmds[i++], r = cmds[i++] * scale();
        ctx.moveTo(tx(cx, cy) + r, ty(cx, cy));
        ctx.arc(tx(cx, cy), ty(cx, cy), r, 0, Math.PI * 2);
        break;
      }

      case Op.StrokeCircle: {
        beginStroke();
        const cx = cmds[i++], cy = cmds[i++], r = cmds[i++] * scale();
        ctx.moveTo(tx(cx, cy) + r, ty(cx, cy));
        ctx.arc(tx(cx, cy), ty(cx, cy), r, 0, Math.PI * 2);
        break;
      }

      case Op.FillRect: {
        beginFill();
        const x0 = cmds[i++], y0 = cmds[i++], x1 = cmds[i++], y1 = cmds[i++];
        // Traced as a polygon, not fillRect, so it can join a batch and so a
        // rotated transform shears it correctly rather than staying upright.
        ctx.moveTo(tx(x0, y0), ty(x0, y0));
        ctx.lineTo(tx(x1, y0), ty(x1, y0));
        ctx.lineTo(tx(x1, y1), ty(x1, y1));
        ctx.lineTo(tx(x0, y1), ty(x0, y1));
        ctx.closePath();
        break;
      }

      case Op.Text: {
        flush();
        const color = readColor();
        const x = cmds[i++], y = cmds[i++], height = cmds[i++];
        const text = strings[cmds[i++]] ?? "";
        // Text is the one thing that needs the real transform: it has to rotate
        // with its gate. The world transform flips Y, so the glyphs are drawn
        // in an unflipped frame at the same point.
        ctx.setTransform(a, b, c, d, e, f);
        ctx.transform(1, 0, 0, -1, x, y);
        ctx.fillStyle = color;
        ctx.font = `${height}px ${font}`;
        // Scene::text hangs the string from the top of its capitals, which is
        // what "hanging" means here -- not the baseline the canvas defaults to.
        ctx.textBaseline = "hanging";
        ctx.fillText(text, 0, 0);
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        break;
      }

      case Op.Arc: {
        beginStroke();
        const cx = cmds[i++], cy = cmds[i++], r = cmds[i++];
        const startDeg = cmds[i++], sweepDeg = cmds[i++];

        // Walked as a polyline in world space rather than handed to ctx.arc.
        // A gate can be rotated, which turns the arc's start and end angles
        // with it, and the world transform flips Y on top of that -- so the
        // angles a canvas would want depend on the whole composed matrix.
        // Stepping the arc where its angles are defined and transforming each
        // point is correct under any transform, and an arc is a handful of
        // vertices against a schematic's thousands.
        //
        // Scene measures angles from +Y, increasing clockwise toward +X.
        const segments = arcSegments(r * scale(), sweepDeg);
        for (let k = 0; k <= segments; k++) {
          const deg = startDeg + (sweepDeg * k) / segments;
          const x = cx + r * Math.sin(deg * DEG);
          const y = cy + r * Math.cos(deg * DEG);
          if (k === 0) ctx.moveTo(tx(x, y), ty(x, y));
          else ctx.lineTo(tx(x, y), ty(x, y));
        }
        break;
      }

      default:
        // An opcode this replayer does not know means the two halves of the
        // wire format have drifted. Nothing downstream can be trusted, so stop.
        flush();
        throw new Error(`unknown scene opcode ${op} at index ${i - 1}`);
    }
  }

  flush();
}
