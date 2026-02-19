/**
 * Export a Yjs circuit document to CDL XML format (desktop CedarLogic native format).
 *
 * CDL coordinate system: Y-up, positions in grid units (divide pixel coords by GRID_SIZE, negate Y).
 * CDL uses sequential integer IDs for gates and wires.
 */
import * as Y from "yjs";
import {
  getGatesMap,
  getWiresMap,
  getConnectionsMap,
} from "./collab/yjs-schema";
import { GRID_SIZE } from "@shared/constants";

function escapeXml(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** Convert web pixel coords to CDL grid coords (Y-up). */
function toCdl(px: number, py: number): [number, number] {
  return [px / GRID_SIZE, -py / GRID_SIZE];
}

export function exportToCdl(doc: Y.Doc, title: string): void {
  const gatesMap = getGatesMap(doc);
  const wiresMap = getWiresMap(doc);
  const connectionsMap = getConnectionsMap(doc);

  // Build UUID → sequential integer ID maps
  const gateIdMap = new Map<string, number>();
  const wireIdMap = new Map<string, number>();
  let nextId = 1;

  gatesMap.forEach((_yGate, uuid) => {
    gateIdMap.set(uuid, nextId++);
  });
  wiresMap.forEach((_yWire, uuid) => {
    wireIdMap.set(uuid, nextId++);
  });
  console.log("[CDL export] Gates:", gateIdMap.size, "Wires:", wireIdMap.size, "Connections:", connectionsMap.size);

  // Build connection index: wireUuid → [{gateUuid, pinName, pinDirection}]
  const wireConnections = new Map<
    string,
    Array<{ gateUuid: string; pinName: string; pinDirection: string }>
  >();
  // Build gate connection index: gateUuid → [{pinName, pinDirection, wireUuid}]
  const gateConnections = new Map<
    string,
    Array<{ pinName: string; pinDirection: string; wireUuid: string }>
  >();

  connectionsMap.forEach((yConn) => {
    const gateUuid = yConn.get("gateId") as string;
    const pinName = yConn.get("pinName") as string;
    const pinDirection = yConn.get("pinDirection") as string;
    const wireUuid = yConn.get("wireId") as string;

    let wArr = wireConnections.get(wireUuid);
    if (!wArr) {
      wArr = [];
      wireConnections.set(wireUuid, wArr);
    }
    wArr.push({ gateUuid, pinName, pinDirection });

    let gArr = gateConnections.get(gateUuid);
    if (!gArr) {
      gArr = [];
      gateConnections.set(gateUuid, gArr);
    }
    gArr.push({ pinName, pinDirection, wireUuid });
  });

  // Build output matching the exact CDL format the desktop parser expects.
  // The desktop uses a custom streaming XMLParser — formatting matters.
  let out = "";

  // Sentinel circuit (for older CedarLogic versions that don't have <version>)
  out += `\n<circuit>\n`;
  out += `<CurrentPage>0</CurrentPage>\n`;
  out += `<page 0>\n`;
  out += `<PageViewport>-32.95,39.6893,61.95,-63.2229</PageViewport>\n`;
  out += `<gate>\n`;
  out += `<ID>2</ID>\n`;
  out += `<type>AA_LABEL</type>\n`;
  out += `<position>14.5,-9.5</position>\n`;
  out += `<gparam>LABEL_TEXT This file was exported from CedarLogic Web.</gparam>\n`;
  out += `<gparam>TEXT_HEIGHT 2</gparam>\n`;
  out += `<gparam>angle 0.0</gparam></gate></page 0>\n`;
  out += `</circuit>\n`;
  out += `<throw_away></throw_away>\n`;

  // Version tag + real circuit (note: tab before version matches desktop format)
  out += `\t<version>2.0 | web</version>`;
  out += `<circuit>\n`;
  out += `<CurrentPage>0</CurrentPage>\n`;
  out += `<page 0>\n`;

  // Gates
  gatesMap.forEach((yGate, uuid) => {
    const intId = gateIdMap.get(uuid)!;
    const defId = yGate.get("defId") as string;
    const px = yGate.get("x") as number;
    const py = yGate.get("y") as number;
    const rotation = (yGate.get("rotation") as number) ?? 0;
    const [cx, cy] = toCdl(px, py);

    out += `<gate>\n`;
    out += `<ID>${intId}</ID>\n`;
    out += `<type>${escapeXml(defId)}</type>\n`;
    out += `<position>${cx},${cy}</position>\n`;

    // Connections (input/output pins) — must match desktop format exactly:
    // <output>\n<ID>pinName</ID>wireId </output>\n
    const conns = gateConnections.get(uuid) || [];
    for (const conn of conns) {
      const tag = conn.pinDirection === "input" ? "input" : "output";
      const wireIntId = wireIdMap.get(conn.wireUuid) ?? 0;
      out += `<${tag}>\n`;
      out += `<ID>${escapeXml(conn.pinName)}</ID>${wireIntId} </${tag}>\n`;
    }

    // GUI param: angle
    out += `<gparam>angle ${rotation.toFixed(1)}</gparam>\n`;

    // Logic params
    for (const [k, v] of yGate.entries()) {
      if (k.startsWith("param:")) {
        const paramName = k.slice(6);
        out += `<lparam>${escapeXml(paramName)} ${escapeXml(String(v))}</lparam>\n`;
      }
    }

    out += `</gate>\n`;
  });

  // Wires
  wiresMap.forEach((yWire, uuid) => {
    const intId = wireIdMap.get(uuid)!;
    let segments: Array<{ x1: number; y1: number; x2: number; y2: number }>;
    try {
      segments = JSON.parse(yWire.get("segments") || "[]");
    } catch {
      segments = [];
    }

    out += `<wire>\n`;
    out += `<ID>${intId} </ID>\n`;
    out += `<shape>\n`;

    const conns = wireConnections.get(uuid) || [];

    for (let i = 0; i < segments.length; i++) {
      const seg = segments[i];
      const [x1, y1] = toCdl(seg.x1, seg.y1);
      const [x2, y2] = toCdl(seg.x2, seg.y2);
      const isVertical = seg.x1 === seg.x2;
      const segTag = isVertical ? "vsegment" : "hsegment";

      out += `<${segTag}>\n`;
      out += `<ID>${i}</ID>\n`;
      out += `<points>${x1},${y1},${x2},${y2}</points>\n`;

      // Attach connections to the first/last segment
      if (i === 0 || i === segments.length - 1) {
        for (const conn of conns) {
          if (
            (conn.pinDirection === "output" && i === 0) ||
            (conn.pinDirection === "input" && i === segments.length - 1)
          ) {
            const gateIntId = gateIdMap.get(conn.gateUuid) ?? 0;
            out += `<connection>\n`;
            out += `<GID>${gateIntId}</GID>\n`;
            out += `<name>${escapeXml(conn.pinName)}</name>\n`;
            out += `</connection>\n`;
          }
        }
      }

      out += `</${segTag}>\n`;
    }

    out += `</shape>\n`;
    out += `</wire>\n`;
  });

  out += `</page 0>`;
  out += `</circuit>`;

  // Trigger download
  const blob = new Blob([out], { type: "application/xml" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${title || "circuit"}.cdl`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
