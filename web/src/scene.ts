// Replay a recorded CedarLogic frame onto a Canvas2D context.
//
// This is the other half of wasm/gui/SceneBuffer.h. The core records a frame as
// one packed float32 stream and hands it over as a view into wasm memory; this
// walks it once and issues the matching canvas calls. The two files describe
// the same wire format and have to move together.
//
// Nothing here decides how the circuit looks -- every colour, width and
// coordinate was chosen by the same C++ that draws the desktop. This is a
// transcription, and it should stay one.

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

  // The viewport is a transform like any other, but it is the one every later
  // transform composes onto, so it is applied as the base of the canvas stack
  // rather than tracked separately.
  ctx.save();
  ctx.scale(ratio, ratio);
  let viewportDepth = 0;

  const readColor = (): string => {
    const r = Math.round(cmds[i++] * 255);
    const g = Math.round(cmds[i++] * 255);
    const b = Math.round(cmds[i++] * 255);
    const a = cmds[i++];
    return `rgba(${r},${g},${b},${a})`;
  };

  // Stroke width is in device pixels, but the canvas transform is in world
  // units, so a nominal width would be scaled by the zoom. Undo that: read the
  // current horizontal scale and divide, which keeps a 1px line 1px at any zoom
  // -- the same thing the GL and Skia backends do.
  const applyStroke = (): void => {
    ctx.strokeStyle = readColor();
    const width = cmds[i++];
    const cap = cmds[i++];
    const dashed = cmds[i++];
    const scale = Math.abs(ctx.getTransform().a) || 1;
    ctx.lineWidth = width / scale;
    ctx.lineCap = CAPS[cap] ?? "butt";
    ctx.setLineDash(dashed ? DASH_PATTERN.map((d) => d / scale) : []);
  };

  const tracePoints = (count: number, closed: boolean): void => {
    ctx.beginPath();
    ctx.moveTo(cmds[i], cmds[i + 1]);
    for (let p = 1; p < count; p++) {
      ctx.lineTo(cmds[i + p * 2], cmds[i + p * 2 + 1]);
    }
    i += count * 2;
    if (closed) ctx.closePath();
  };

  while (i < cmds.length) {
    const op = cmds[i++];

    switch (op) {
      case Op.SetViewport: {
        // A new viewport replaces the old one rather than composing with it.
        if (viewportDepth > 0) ctx.restore();
        ctx.save();
        viewportDepth = 1;
        ctx.transform(cmds[i], cmds[i + 1], cmds[i + 2], cmds[i + 3], cmds[i + 4], cmds[i + 5]);
        i += 6;
        break;
      }

      case Op.PushTransform: {
        ctx.save();
        ctx.transform(cmds[i], cmds[i + 1], cmds[i + 2], cmds[i + 3], cmds[i + 4], cmds[i + 5]);
        i += 6;
        break;
      }

      case Op.PopTransform: {
        ctx.restore();
        break;
      }

      case Op.Lines: {
        applyStroke();
        // Disconnected pairs: p0-p1, p2-p3, ... One path for the whole batch,
        // which is what makes a grid of hundreds of lines a single stroke call.
        const count = cmds[i++];
        ctx.beginPath();
        for (let p = 0; p + 1 < count; p += 2) {
          ctx.moveTo(cmds[i + p * 2], cmds[i + p * 2 + 1]);
          ctx.lineTo(cmds[i + (p + 1) * 2], cmds[i + (p + 1) * 2 + 1]);
        }
        i += count * 2;
        ctx.stroke();
        break;
      }

      case Op.Polyline: {
        applyStroke();
        const closed = cmds[i++] !== 0;
        const count = cmds[i++];
        tracePoints(count, closed);
        ctx.stroke();
        break;
      }

      case Op.FillPolygon: {
        ctx.fillStyle = readColor();
        const count = cmds[i++];
        tracePoints(count, true);
        ctx.fill();
        break;
      }

      case Op.FillCircle: {
        ctx.fillStyle = readColor();
        const cx = cmds[i++], cy = cmds[i++], r = cmds[i++];
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, Math.PI * 2);
        ctx.fill();
        break;
      }

      case Op.StrokeCircle: {
        applyStroke();
        const cx = cmds[i++], cy = cmds[i++], r = cmds[i++];
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, Math.PI * 2);
        ctx.stroke();
        break;
      }

      case Op.FillRect: {
        ctx.fillStyle = readColor();
        const x0 = cmds[i++], y0 = cmds[i++], x1 = cmds[i++], y1 = cmds[i++];
        ctx.fillRect(Math.min(x0, x1), Math.min(y0, y1), Math.abs(x1 - x0), Math.abs(y1 - y0));
        break;
      }

      case Op.Text: {
        ctx.fillStyle = readColor();
        const x = cmds[i++], y = cmds[i++], height = cmds[i++];
        const text = strings[cmds[i++]] ?? "";
        // Scene::text hangs the string from the top of its capitals, which is
        // what "hanging" means here -- not the baseline the canvas defaults to.
        ctx.save();
        // The world transform flips Y, so text drawn under it would come out
        // upside down. Draw it in an unflipped local frame at the same point.
        ctx.translate(x, y);
        ctx.scale(1, -1);
        ctx.font = `${height}px ${font}`;
        ctx.textBaseline = "hanging";
        ctx.fillText(text, 0, 0);
        ctx.restore();
        break;
      }

      case Op.Arc: {
        applyStroke();
        const cx = cmds[i++], cy = cmds[i++], r = cmds[i++];
        const startDeg = cmds[i++], sweepDeg = cmds[i++];
        // Scene measures angles from +Y, clockwise toward +X; canvas measures
        // from +X, counter-clockwise. Rotating by -90 degrees and negating
        // converts between them.
        const toCanvas = (deg: number) => ((90 - deg) * Math.PI) / 180;
        ctx.beginPath();
        ctx.arc(cx, cy, r, toCanvas(startDeg), toCanvas(startDeg + sweepDeg), sweepDeg > 0);
        ctx.stroke();
        break;
      }

      default:
        // An opcode this replayer does not know means the two halves of the
        // wire format have drifted. Nothing downstream can be trusted, so stop.
        throw new Error(`unknown scene opcode ${op} at index ${i - 1}`);
    }
  }

  if (viewportDepth > 0) ctx.restore();
  ctx.restore();
}
