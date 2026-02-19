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
  readWireModel,
} from "./collab/yjs-schema";
import { GRID_SIZE } from "@shared/constants";
import type { WireModel } from "@shared/wire-types";

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

    let gArr = gateConnections.get(gateUuid);
    if (!gArr) {
      gArr = [];
      gateConnections.set(gateUuid, gArr);
    }
    gArr.push({ pinName, pinDirection, wireUuid });
  });

  // Build gate UUID→intId lookup for writing connections on segments
  const gateUuidToIntId = new Map<string, number>();
  gatesMap.forEach((_yGate, uuid) => {
    gateUuidToIntId.set(uuid, gateIdMap.get(uuid)!);
  });

  // Build output matching the exact CDL format the desktop parser expects.
  let out = "";

  // Sentinel circuit
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

  // Version tag + real circuit
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

    const conns = gateConnections.get(uuid) || [];
    for (const conn of conns) {
      const tag = conn.pinDirection === "input" ? "input" : "output";
      const wireIntId = wireIdMap.get(conn.wireUuid) ?? 0;
      out += `<${tag}>\n`;
      out += `<ID>${escapeXml(conn.pinName)}</ID>${wireIntId} </${tag}>\n`;
    }

    out += `<gparam>angle ${rotation.toFixed(1)}</gparam>\n`;

    for (const [k, v] of yGate.entries()) {
      if (k.startsWith("param:")) {
        const paramName = k.slice(6);
        out += `<lparam>${escapeXml(paramName)} ${escapeXml(String(v))}</lparam>\n`;
      }
    }

    out += `</gate>\n`;
  });

  // Wires — write from WireModel segment tree
  wiresMap.forEach((yWire, uuid) => {
    const intId = wireIdMap.get(uuid)!;
    const model = readWireModel(yWire);

    out += `<wire>\n`;
    out += `<ID>${intId} </ID>\n`;
    out += `<shape>\n`;

    if (model) {
      for (const seg of Object.values(model.segMap)) {
        const segTag = seg.vertical ? "vsegment" : "hsegment";
        const [x1, y1] = toCdl(seg.begin.x, seg.begin.y);
        const [x2, y2] = toCdl(seg.end.x, seg.end.y);

        out += `<${segTag}>\n`;
        out += `<ID>${seg.id}</ID>\n`;
        out += `<points>${x1},${y1},${x2},${y2}</points>\n`;

        // Write per-segment connections
        for (const conn of seg.connections) {
          const gateIntId = gateUuidToIntId.get(conn.gateId) ?? 0;
          out += `<connection>\n`;
          out += `<GID>${gateIntId}</GID>\n`;
          out += `<name>${escapeXml(conn.pinName)}</name>\n`;
          out += `</connection>\n`;
        }

        // Write intersections
        for (const [posStr, ids] of Object.entries(seg.intersects)) {
          const webPos = Number(posStr);
          // Convert web position back to CDL position
          const cdlPos = seg.vertical
            ? -webPos / GRID_SIZE  // Y: negate and divide
            : webPos / GRID_SIZE;   // X: just divide
          for (const otherId of ids) {
            out += `<intersection>${cdlPos} ${otherId}</intersection>\n`;
          }
        }

        out += `</${segTag}>\n`;
      }
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
