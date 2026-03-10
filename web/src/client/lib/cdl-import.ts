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
  setPageList,
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
  let result = text.replace(/<(\/?)page\s+(\d+)>/g, "<$1page_$2>");
  // Escape bare & characters that aren't already XML entities
  result = result.replace(/&(?!amp;|lt;|gt;|apos;|quot;|#\d+;|#x[\da-fA-F]+;)/g, "&amp;");
  return result;
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
  page: string;
}

interface ParsedWire {
  uuid: string;
  model: WireModel;
  page: string;
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

  // Find ALL page elements (page_0, page_1, etc.)
  const pageElements: Array<{ el: Element; pageId: string }> = [];
  console.log("[CDL import] Circuit children:", Array.from(circuit.children).map(c => c.tagName));
  for (const child of circuit.children) {
    const tag = child.tagName.toLowerCase();
    if (tag.startsWith("page_")) {
      const pageId = tag.replace("page_", "");
      pageElements.push({ el: child, pageId });
    }
  }

  if (pageElements.length === 0) {
    console.warn("[CDL import] No <page_N> found, using circuit as container (page 0)");
    pageElements.push({ el: circuit, pageId: "0" });
  } else {
    console.log("[CDL import] Found pages:", pageElements.map(p => p.pageId));
  }

  // Integer ID → UUID maps (global across all pages)
  const gateIntToUuid = new Map<string, string>();
  const wireIntToUuid = new Map<string, string>();

  const allParsedGates: ParsedGate[] = [];
  const allParsedWires: ParsedWire[] = [];
  const pageIds: string[] = [];

  // Parse each page
  for (const { el: pageEl, pageId } of pageElements) {
    pageIds.push(pageId);

    // Parse gates
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

      let rotation = 0;
      const params: Record<string, string> = {};

      for (const paramEl of gateEl.querySelectorAll("gparam")) {
        const text = paramEl.textContent?.trim() ?? "";
        const spaceIdx = text.indexOf(" ");
        if (spaceIdx === -1) continue;
        const key = text.slice(0, spaceIdx);
        const val = text.slice(spaceIdx + 1);
        if (key === "angle") {
          rotation = -(parseFloat(val) || 0);
        } else {
          params[key] = val;
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

      allParsedGates.push({
        uuid, intId, defId, logicType,
        x: pos.x, y: pos.y, rotation, params,
        inputConns, outputConns, page: pageId,
      });
    }

    // Build gate int ID → parsed gate lookup (for wire connection direction)
    const gateByIntId = new Map<string, ParsedGate>();
    for (const g of allParsedGates) {
      gateByIntId.set(g.intId, g);
    }

    // Parse wires
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

          for (const connEl of segEl.querySelectorAll("connection")) {
            const gidEl = connEl.querySelector("GID");
            const nameEl = connEl.querySelector("name");
            if (!gidEl || !nameEl) continue;
            const gid = gidEl.textContent?.trim() ?? "";
            const connName = nameEl.textContent?.trim() ?? "";
            const gateUuid = gateIntToUuid.get(gid);
            if (gateUuid) {
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

          for (const isectEl of segEl.querySelectorAll("intersection")) {
            const text = isectEl.textContent?.trim() ?? "";
            const parts = text.split(/\s+/);
            if (parts.length >= 2) {
              const cdlPos = parseFloat(parts[0]);
              const cdlOtherId = parseInt(parts[1], 10);
              const webPos = isVertical
                ? -cdlPos * GRID_SIZE
                : cdlPos * GRID_SIZE;
              if (!seg.intersects[webPos]) seg.intersects[webPos] = [];
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

      allParsedWires.push({ uuid, model, page: pageId });
    }
  }

  console.log("[CDL import] Parsed", allParsedGates.length, "gates,", allParsedWires.length, "wires across", pageIds.length, "pages");

  // Count connections that will be created
  let connCount = 0;
  for (const g of allParsedGates) {
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
    // Add gates with page
    for (const g of allParsedGates) {
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
      addGateToDoc(doc, g.uuid, gateData as any, g.page);
    }

    // Add wires as WireModel with page
    for (const w of allParsedWires) {
      addWireModelToDoc(doc, w.uuid, w.model, w.page);
    }

    // Add connections from gate input/output data
    for (const g of allParsedGates) {
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

    // Store page list in meta
    if (pageIds.length > 1 || (pageIds.length === 1 && pageIds[0] !== "0")) {
      setPageList(doc, pageIds);
    }
  });
}
