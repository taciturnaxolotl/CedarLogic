/**
 * Import a CDL XML file into a Yjs circuit document.
 *
 * CDL coordinate system: Y-up, positions in grid units.
 * Web coordinate system: Y-down, positions in pixels.
 * Transform: multiply by GRID_SIZE, negate Y.
 */
import * as Y from "yjs";
import {
  addGateToDoc,
  addWireToDoc,
  addConnectionToDoc,
} from "./collab/yjs-schema";
import { GRID_SIZE } from "@shared/constants";
import type { GateDefinition } from "@shared/types";

/** Convert CDL grid coords (Y-up) to web pixel coords (Y-down). */
function toWeb(gx: number, gy: number): { x: number; y: number } {
  return { x: gx * GRID_SIZE, y: -gy * GRID_SIZE };
}

/**
 * Preprocess CDL text into valid XML.
 * CDL has `<page 0>` tags (spaces in tag names) which aren't valid XML,
 * and we must avoid HTML parsing because `<input>` is a void element.
 */
function preprocessCdl(text: string): string {
  // Replace <page N> and </page N> with <page_N> and </page_N>
  return text.replace(/<(\/?)page\s+(\d+)>/g, "<$1page_$2>");
}

export function importFromCdl(
  doc: Y.Doc,
  cdlText: string,
  gateDefs: GateDefinition[]
): void {
  console.log("[CDL import] Starting import, text length:", cdlText.length, "gateDefs:", gateDefs.length);
  const xmlText = preprocessCdl(cdlText);
  const parser = new DOMParser();
  const xmlDoc = parser.parseFromString(xmlText, "text/xml");

  // Check for parse errors
  const parseError = xmlDoc.querySelector("parsererror");
  if (parseError) {
    console.warn("[CDL import] Direct XML parse failed, trying <root> wrapper...");
    // Try wrapping in a root element — CDL files have multiple root-level elements
    const wrapped = `<root>${xmlText}</root>`;
    const xmlDoc2 = parser.parseFromString(wrapped, "text/xml");
    const parseError2 = xmlDoc2.querySelector("parsererror");
    if (parseError2) {
      console.error("[CDL import] Wrapped parse also failed:", parseError2.textContent);
      return;
    }
    console.log("[CDL import] Wrapped parse succeeded");
    return importFromXmlDoc(doc, xmlDoc2, gateDefs);
  }

  console.log("[CDL import] Direct XML parse succeeded");
  importFromXmlDoc(doc, xmlDoc, gateDefs);
}

function importFromXmlDoc(
  doc: Y.Doc,
  xmlDoc: Document,
  gateDefs: GateDefinition[]
): void {
  // Build defId → logicType lookup
  const defToLogic = new Map<string, string>();
  for (const def of gateDefs) {
    defToLogic.set(def.id, def.logicType);
  }

  // Find the actual circuit data.
  // CDL 2.0+ has a sentinel circuit, then <throw_away>, then <version>, then the real <circuit>.
  // We need the last <circuit> element in the document.
  const circuits = xmlDoc.querySelectorAll("circuit");
  console.log("[CDL import] Found", circuits.length, "circuit element(s)");
  const circuit =
    circuits.length > 1 ? circuits[circuits.length - 1] : circuits[0];
  if (!circuit) {
    console.error("[CDL import] No <circuit> element found");
    return;
  }

  // Find first page element (page_0, page_1, etc.)
  let pageEl: Element | null = null;
  console.log("[CDL import] Circuit children:", Array.from(circuit.children).map(c => c.tagName));
  for (const child of circuit.children) {
    if (child.tagName.toLowerCase().startsWith("page_")) {
      pageEl = child;
      break;
    }
  }
  if (!pageEl) {
    console.warn("[CDL import] No <page_N> found, using circuit as container");
    pageEl = circuit;
  } else {
    console.log("[CDL import] Using page element:", pageEl.tagName);
  }

  // Integer ID → UUID maps
  const gateIntToUuid = new Map<string, string>();
  const wireIntToUuid = new Map<string, string>();

  // Parse gates first to build ID map
  interface ParsedGate {
    uuid: string;
    defId: string;
    logicType: string;
    x: number;
    y: number;
    rotation: number;
    params: Record<string, string>;
    inputConns: Array<{ pinName: string; wireIntIds: string[] }>;
    outputConns: Array<{ pinName: string; wireIntIds: string[] }>;
  }

  const parsedGates: ParsedGate[] = [];

  for (const gateEl of pageEl.querySelectorAll("gate")) {
    const idEl = gateEl.querySelector("ID");
    const typeEl = gateEl.querySelector("type");
    const posEl = gateEl.querySelector("position");
    if (!idEl || !typeEl || !posEl) continue;

    const intId = idEl.textContent?.trim() ?? "";
    const defId = typeEl.textContent?.trim() ?? "";
    const posText = posEl.textContent?.trim() ?? "0,0";

    const [gx, gy] = posText.split(",").map(Number);
    const pos = toWeb(gx, gy);
    const uuid = crypto.randomUUID();
    gateIntToUuid.set(intId, uuid);

    // Parse rotation from gparam angle
    let rotation = 0;
    const params: Record<string, string> = {};

    for (const paramEl of gateEl.querySelectorAll("gparam")) {
      const text = paramEl.textContent?.trim() ?? "";
      const spaceIdx = text.indexOf(" ");
      if (spaceIdx === -1) continue;
      const key = text.slice(0, spaceIdx);
      const val = text.slice(spaceIdx + 1);
      if (key === "angle") {
        rotation = parseFloat(val) || 0;
      }
    }

    for (const paramEl of gateEl.querySelectorAll("lparam")) {
      const text = paramEl.textContent?.trim() ?? "";
      const spaceIdx = text.indexOf(" ");
      if (spaceIdx === -1) continue;
      const key = text.slice(0, spaceIdx);
      const val = text.slice(spaceIdx + 1);
      params[key] = val;
    }

    // Parse input/output connections
    const inputConns: Array<{ pinName: string; wireIntIds: string[] }> = [];
    const outputConns: Array<{ pinName: string; wireIntIds: string[] }> = [];

    for (const inputEl of gateEl.querySelectorAll("input")) {
      const pinIdEl = inputEl.querySelector("ID");
      const pinName = pinIdEl?.textContent?.trim() ?? "";
      // Wire IDs are the direct text content of <input> (excluding child elements)
      const wireIntIds: string[] = [];
      for (const node of inputEl.childNodes) {
        if (node.nodeType === Node.TEXT_NODE) {
          const parts = (node.textContent ?? "").trim().split(/\s+/);
          for (const p of parts) {
            if (/^\d+$/.test(p)) wireIntIds.push(p);
          }
        }
      }
      inputConns.push({ pinName, wireIntIds });
    }

    for (const outputEl of gateEl.querySelectorAll("output")) {
      const pinIdEl = outputEl.querySelector("ID");
      const pinName = pinIdEl?.textContent?.trim() ?? "";
      const wireIntIds: string[] = [];
      for (const node of outputEl.childNodes) {
        if (node.nodeType === Node.TEXT_NODE) {
          const parts = (node.textContent ?? "").trim().split(/\s+/);
          for (const p of parts) {
            if (/^\d+$/.test(p)) wireIntIds.push(p);
          }
        }
      }
      outputConns.push({ pinName, wireIntIds });
    }

    const logicType = defToLogic.get(defId) ?? "";
    if (!logicType) {
      console.warn("[CDL import] No logicType found for defId:", defId);
    }

    parsedGates.push({
      uuid,
      defId,
      logicType,
      x: pos.x,
      y: pos.y,
      rotation,
      params,
      inputConns,
      outputConns,
    });
  }

  // Parse wires
  interface ParsedWire {
    uuid: string;
    segments: Array<{ x1: number; y1: number; x2: number; y2: number }>;
  }

  const parsedWires: ParsedWire[] = [];

  for (const wireEl of pageEl.querySelectorAll("wire")) {
    const idEl = wireEl.querySelector("ID");
    if (!idEl) continue;
    // Wire IDs can be space-separated (bus wires); use first ID as primary
    const idText = idEl.textContent?.trim() ?? "";
    const primaryIntId = idText.split(/\s+/)[0];

    const uuid = crypto.randomUUID();
    wireIntToUuid.set(primaryIntId, uuid);
    // Also map all IDs for bus wires
    for (const wid of idText.split(/\s+/).filter(Boolean)) {
      if (!wireIntToUuid.has(wid)) {
        wireIntToUuid.set(wid, uuid);
      }
    }

    const segments: Array<{
      x1: number;
      y1: number;
      x2: number;
      y2: number;
    }> = [];

    const shapeEl = wireEl.querySelector("shape");
    if (shapeEl) {
      for (const segEl of shapeEl.querySelectorAll("hsegment, vsegment")) {
        const pointsEl = segEl.querySelector("points");
        if (!pointsEl) continue;
        const pts = (pointsEl.textContent?.trim() ?? "")
          .split(",")
          .map(Number);
        if (pts.length < 4) continue;
        const p1 = toWeb(pts[0], pts[1]);
        const p2 = toWeb(pts[2], pts[3]);
        segments.push({ x1: p1.x, y1: p1.y, x2: p2.x, y2: p2.y });
      }
    }

    parsedWires.push({ uuid, segments });
  }

  console.log("[CDL import] Parsed", parsedGates.length, "gates,", parsedWires.length, "wires");
  if (parsedGates.length > 0) {
    const g = parsedGates[0];
    console.log("[CDL import] First gate:", { defId: g.defId, x: g.x, y: g.y, inputs: g.inputConns.length, outputs: g.outputConns.length });
  }
  if (parsedWires.length > 0) {
    const w = parsedWires[0];
    console.log("[CDL import] First wire:", { segments: w.segments.length, seg0: w.segments[0] });
  }

  // Count connections that will be created
  let connCount = 0;
  for (const g of parsedGates) {
    for (const conn of [...g.inputConns, ...g.outputConns]) {
      for (const wireIntId of conn.wireIntIds) {
        if (wireIntToUuid.has(wireIntId)) connCount++;
        else console.warn("[CDL import] Wire int ID not found:", wireIntId, "for gate", g.defId, "pin", conn.pinName);
      }
    }
  }
  console.log("[CDL import] Will create", connCount, "connections");

  // Now apply everything to the doc in a single transaction
  doc.transact(() => {
    // Add gates
    for (const g of parsedGates) {
      const gateData: Record<string, any> = {
        defId: g.defId,
        logicType: g.logicType,
        x: g.x,
        y: g.y,
        rotation: g.rotation,
      };
      for (const [k, v] of Object.entries(g.params)) {
        gateData[`param:${k}`] = v;
      }
      addGateToDoc(doc, g.uuid, gateData as any);
    }

    // Add wires
    for (const w of parsedWires) {
      addWireToDoc(doc, w.uuid, w.segments);
    }

    // Add connections from gate input/output data
    for (const g of parsedGates) {
      for (const conn of g.inputConns) {
        for (const wireIntId of conn.wireIntIds) {
          const wireUuid = wireIntToUuid.get(wireIntId);
          if (wireUuid) {
            addConnectionToDoc(doc, g.uuid, conn.pinName, "input", wireUuid);
          }
        }
      }
      for (const conn of g.outputConns) {
        for (const wireIntId of conn.wireIntIds) {
          const wireUuid = wireIntToUuid.get(wireIntId);
          if (wireUuid) {
            addConnectionToDoc(doc, g.uuid, conn.pinName, "output", wireUuid);
          }
        }
      }
    }
  });
}
