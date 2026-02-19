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
  addWireModelToDoc,
  addConnectionToDoc,
} from "./collab/yjs-schema";
import { GRID_SIZE } from "@shared/constants";
import type { GateDefinition } from "@shared/types";
import type { WireModel, WireSegmentNode, WireConnection } from "@shared/wire-types";

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
    intId: string;
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
      intId,
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

  // Build gate int ID → parsed gate lookup
  const gateByIntId = new Map<string, ParsedGate>();
  for (const g of parsedGates) {
    gateByIntId.set(g.intId, g);
  }

  // Parse wires — build full WireModel with segment tree
  interface ParsedWire {
    uuid: string;
    model: WireModel;
  }

  const parsedWires: ParsedWire[] = [];

  for (const wireEl of pageEl.querySelectorAll("wire")) {
    const idEl = wireEl.querySelector("ID");
    if (!idEl) continue;
    const idText = idEl.textContent?.trim() ?? "";
    const primaryIntId = idText.split(/\s+/)[0];

    const uuid = crypto.randomUUID();
    wireIntToUuid.set(primaryIntId, uuid);
    for (const wid of idText.split(/\s+/).filter(Boolean)) {
      if (!wireIntToUuid.has(wid)) {
        wireIntToUuid.set(wid, uuid);
      }
    }

    const model: WireModel = { segMap: {}, headSegment: 0, nextSegId: 0 };
    // Map from CDL segment int ID to our segment int ID (they may differ)
    const cdlSegIdMap = new Map<number, number>();

    const shapeEl = wireEl.querySelector("shape");
    if (shapeEl) {
      for (const segEl of shapeEl.querySelectorAll("hsegment, vsegment")) {
        const isVertical = segEl.tagName.toLowerCase() === "vsegment";
        const segIdEl = segEl.querySelector("ID");
        const pointsEl = segEl.querySelector("points");
        if (!pointsEl) continue;

        const cdlSegId = parseInt(segIdEl?.textContent?.trim() ?? "0", 10);
        const pts = (pointsEl.textContent?.trim() ?? "").split(",").map(Number);
        if (pts.length < 4) continue;

        const p1 = toWeb(pts[0], pts[1]);
        const p2 = toWeb(pts[2], pts[3]);
        const segId = model.nextSegId++;

        cdlSegIdMap.set(cdlSegId, segId);

        const seg: WireSegmentNode = {
          id: segId,
          vertical: isVertical,
          begin: { x: Math.min(p1.x, p2.x), y: Math.min(p1.y, p2.y) },
          end: { x: Math.max(p1.x, p2.x), y: Math.max(p1.y, p2.y) },
          connections: [],
          intersects: {},
        };

        // Parse per-segment connections
        for (const connEl of segEl.querySelectorAll("connection")) {
          const gidEl = connEl.querySelector("GID");
          const nameEl = connEl.querySelector("name");
          if (!gidEl || !nameEl) continue;
          const gid = gidEl.textContent?.trim() ?? "";
          const connName = nameEl.textContent?.trim() ?? "";
          const gateUuid = gateIntToUuid.get(gid);
          if (gateUuid) {
            // Determine pin direction from gate data
            const pg = gateByIntId.get(gid);
            let pinDir: "input" | "output" = "input";
            if (pg) {
              for (const oc of pg.outputConns) {
                if (oc.pinName === connName) { pinDir = "output"; break; }
              }
            }
            seg.connections.push({
              gateId: gateUuid,
              pinName: connName,
              pinDirection: pinDir,
            });
          }
        }

        // Parse intersections (will remap IDs after all segments parsed)
        for (const isectEl of segEl.querySelectorAll("intersection")) {
          const text = isectEl.textContent?.trim() ?? "";
          const parts = text.split(/\s+/);
          if (parts.length >= 2) {
            const cdlPos = parseFloat(parts[0]);
            const cdlOtherId = parseInt(parts[1], 10);
            // Convert CDL position to web coords
            // For vertical segments, the key is Y (CDL Y-up → web Y-down)
            // For horizontal segments, the key is X (CDL → web)
            const webPos = isVertical
              ? -cdlPos * GRID_SIZE  // Y coordinate
              : cdlPos * GRID_SIZE;   // X coordinate
            if (!seg.intersects[webPos]) seg.intersects[webPos] = [];
            // Store CDL segment ID temporarily; we'll remap below
            seg.intersects[webPos].push(cdlOtherId);
          }
        }

        model.segMap[segId] = seg;
      }
    }

    // Remap intersection segment IDs from CDL IDs to our IDs
    for (const seg of Object.values(model.segMap)) {
      const newIntersects: Record<number, number[]> = {};
      for (const [posStr, cdlIds] of Object.entries(seg.intersects)) {
        const pos = Number(posStr);
        const mappedIds: number[] = [];
        for (const cdlId of cdlIds) {
          const mapped = cdlSegIdMap.get(cdlId);
          if (mapped !== undefined) mappedIds.push(mapped);
        }
        if (mappedIds.length > 0) newIntersects[pos] = mappedIds;
      }
      seg.intersects = newIntersects;
    }

    if (Object.keys(model.segMap).length > 0) {
      model.headSegment = Number(Object.keys(model.segMap)[0]);
    }

    parsedWires.push({ uuid, model });
  }

  console.log("[CDL import] Parsed", parsedGates.length, "gates,", parsedWires.length, "wires");

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

    // Add wires as WireModel
    for (const w of parsedWires) {
      addWireModelToDoc(doc, w.uuid, w.model);
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
