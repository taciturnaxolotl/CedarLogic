/**
 * Parses res/cl_gatedefs.xml into a structured JSON file for the canvas renderer.
 * Run with: bun run parse-gates
 */

import { readFileSync, writeFileSync, mkdirSync } from "fs";
import { resolve, dirname } from "path";

const INPUT_PATH = resolve(__dirname, "../../res/cl_gatedefs.xml");
const OUTPUT_PATH = resolve(
  __dirname,
  "../src/client/lib/canvas/gate-defs.json"
);

interface Pin {
  name: string;
  x: number;
  y: number;
  inverted?: boolean;
}

interface LineSegment {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

interface CircleDef {
  cx: number;
  cy: number;
  r: number;
}

interface GateDef {
  id: string;
  name: string;
  caption: string;
  library: string;
  logicType: string;
  guiType: string;
  params: Record<string, string>;
  guiParams: Record<string, string>;
  inputs: Pin[];
  outputs: Pin[];
  shape: LineSegment[];
  circles: CircleDef[];
}

function parsePoint(text: string): { x: number; y: number } {
  const parts = text.split(",").map((s) => parseFloat(s.trim()));
  return { x: parts[0], y: parts[1] };
}

function parseLine(text: string): LineSegment {
  const parts = text.split(",").map((s) => parseFloat(s.trim()));
  return { x1: parts[0], y1: parts[1], x2: parts[2], y2: parts[3] };
}

function extractTag(text: string, tag: string): string | null {
  const re = new RegExp(`<${tag}>([\\s\\S]*?)</${tag}>`);
  const m = text.match(re);
  return m ? m[1].trim() : null;
}

function extractAllTags(
  text: string,
  tag: string
): Array<{ content: string; index: number }> {
  const re = new RegExp(`<${tag}>([\\s\\S]*?)</${tag}>`, "g");
  const results: Array<{ content: string; index: number }> = [];
  let m;
  while ((m = re.exec(text)) !== null) {
    results.push({ content: m[1].trim(), index: m.index });
  }
  return results;
}

function stripComments(text: string): string {
  return text
    .split("\n")
    .filter((line) => !line.trim().startsWith("#"))
    .join("\n");
}

const raw = readFileSync(INPUT_PATH, "utf-8");
const cleaned = stripComments(raw);

// Split into libraries
const libraryBlocks = extractAllTags(cleaned, "library");
const allGates: GateDef[] = [];

for (const lib of libraryBlocks) {
  const libraryName = extractTag(`<library>${lib.content}</library>`, "name");
  if (!libraryName) continue;

  const gateBlocks = extractAllTags(lib.content, "gate");

  for (const gate of gateBlocks) {
    const content = gate.content;
    const name = extractTag(`<x>${content}</x>`, "name") ?? extractTag(`<gate>${content}</gate>`, "name");
    const gateName = extractTag(`<n>${content}</n>`, "name");

    // Re-extract from raw content
    const nameMatch = content.match(/<name>([\s\S]*?)<\/name>/);
    const captionMatch = content.match(/<caption>([\s\S]*?)<\/caption>/);
    const logicTypeMatch = content.match(/<logic_type>([\s\S]*?)<\/logic_type>/);
    const guiTypeMatch = content.match(/<gui_type>([\s\S]*?)<\/gui_type>/);

    if (!nameMatch) continue;

    const gateId = nameMatch[1].trim();
    const caption = captionMatch ? captionMatch[1].trim() : gateId;
    const logicType = logicTypeMatch ? logicTypeMatch[1].trim() : "";
    const guiType = guiTypeMatch ? guiTypeMatch[1].trim() : "";

    // Parse params
    const params: Record<string, string> = {};
    const paramMatches = content.matchAll(/<logic_param>([\s\S]*?)<\/logic_param>/g);
    for (const pm of paramMatches) {
      const paramText = pm[1].trim();
      const spaceIdx = paramText.indexOf(" ");
      if (spaceIdx > 0) {
        params[paramText.substring(0, spaceIdx)] = paramText.substring(spaceIdx + 1).trim();
      }
    }

    // Parse GUI params
    const guiParams: Record<string, string> = {};
    const guiParamMatches = content.matchAll(/<gui_param>([\s\S]*?)<\/gui_param>/g);
    for (const gm of guiParamMatches) {
      const paramText = gm[1].trim();
      const spaceIdx = paramText.indexOf(" ");
      if (spaceIdx > 0) {
        guiParams[paramText.substring(0, spaceIdx)] = paramText.substring(spaceIdx + 1).trim();
      }
    }

    // Parse inputs
    const inputs: Pin[] = [];
    const inputBlocks = extractAllTags(content, "input");
    for (const inp of inputBlocks) {
      const pinName = extractTag(`<x>${inp.content}</x>`, "name") ?? inp.content.match(/<name>([\s\S]*?)<\/name>/)?.[1]?.trim();
      const pointMatch = inp.content.match(/<point>([\s\S]*?)<\/point>/);
      if (pinName && pointMatch) {
        const pt = parsePoint(pointMatch[1].trim());
        const invertedTag = extractTag(`<x>${inp.content}</x>`, "inverted");
        const pin: Pin = { name: pinName, x: pt.x, y: pt.y };
        if (invertedTag === "true") pin.inverted = true;
        inputs.push(pin);
      }
    }

    // Parse outputs
    const outputs: Pin[] = [];
    const outputBlocks = extractAllTags(content, "output");
    for (const out of outputBlocks) {
      const pinName = out.content.match(/<name>([\s\S]*?)<\/name>/)?.[1]?.trim();
      const pointMatch = out.content.match(/<point>([\s\S]*?)<\/point>/);
      if (pinName && pointMatch) {
        const pt = parsePoint(pointMatch[1].trim());
        const invertedTag = extractTag(`<x>${out.content}</x>`, "inverted");
        const pin: Pin = { name: pinName, x: pt.x, y: pt.y };
        if (invertedTag === "true") pin.inverted = true;
        outputs.push(pin);
      }
    }

    // Parse shape lines, handling <offset> blocks that translate child lines
    const shape: LineSegment[] = [];
    const circles: CircleDef[] = [];
    const shapeMatch = content.match(/<shape>([\s\S]*?)<\/shape>/);
    if (shapeMatch) {
      const shapeContent = shapeMatch[1];

      // First, extract offset blocks and replace them with a placeholder so
      // top-level lines don't include offset children
      let topLevelContent = shapeContent;
      const offsetMatches = shapeContent.matchAll(/<offset>([\s\S]*?)<\/offset>/g);
      for (const om of offsetMatches) {
        // Parse the offset point
        const offsetPointMatch = om[1].match(/<point>([\s\S]*?)<\/point>/);
        const offsetPt = offsetPointMatch
          ? parsePoint(offsetPointMatch[1].trim())
          : { x: 0, y: 0 };

        // Parse lines inside this offset block
        const offsetLineMatches = om[1].matchAll(/<line>([\s\S]*?)<\/line>/g);
        for (const lm of offsetLineMatches) {
          try {
            const seg = parseLine(lm[1].trim());
            const round = (n: number) => Math.round(n * 1000) / 1000;
            shape.push({
              x1: round(seg.x1 + offsetPt.x),
              y1: round(seg.y1 + offsetPt.y),
              x2: round(seg.x2 + offsetPt.x),
              y2: round(seg.y2 + offsetPt.y),
            });
          } catch {
            // Skip malformed lines
          }
        }

        // Remove offset block from top-level content
        topLevelContent = topLevelContent.replace(om[0], "");
      }

      // Parse top-level lines (not inside offset blocks)
      const lineMatches = topLevelContent.matchAll(/<line>([\s\S]*?)<\/line>/g);
      for (const lm of lineMatches) {
        try {
          shape.push(parseLine(lm[1].trim()));
        } catch {
          // Skip malformed lines
        }
      }
      // Parse circles (inversion bubbles etc.) — format: cx,cy,radius,segments
      const circleMatches = topLevelContent.matchAll(/<circle>([\s\S]*?)<\/circle>/g);
      for (const cm of circleMatches) {
        try {
          const parts = cm[1].trim().split(",").map((s) => parseFloat(s.trim()));
          circles.push({ cx: parts[0], cy: parts[1], r: parts[2] });
        } catch {
          // Skip malformed circles
        }
      }
    }

    // Flip Y axis: XML uses Y-up (math), canvas uses Y-down (screen)
    allGates.push({
      id: gateId,
      name: gateId,
      caption,
      library: libraryName.trim(),
      logicType,
      guiType,
      params,
      guiParams,
      inputs: inputs.map((p) => ({ ...p, y: -p.y })),
      outputs: outputs.map((p) => ({ ...p, y: -p.y })),
      shape: shape.map((s) => ({ x1: s.x1, y1: -s.y1, x2: s.x2, y2: -s.y2 })),
      circles: circles.map((c) => ({ ...c, cy: -c.cy })),
    });
  }
}

mkdirSync(dirname(OUTPUT_PATH), { recursive: true });
writeFileSync(OUTPUT_PATH, JSON.stringify(allGates, null, 2));
console.log(`Parsed ${allGates.length} gate definitions → ${OUTPUT_PATH}`);
