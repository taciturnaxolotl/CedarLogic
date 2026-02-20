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
import { loadedGateDefs } from "../components/canvas/GateLayer";
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

  // Build gate UUID→intId lookup
  const gateIdMap = new Map<string, number>();
  let nextId = 1;
  gatesMap.forEach((_yGate, uuid) => {
    gateIdMap.set(uuid, nextId++);
  });

  // --- Merge wires that share a gate pin (desktop supports one wire per pin) ---

  // Collect all connections
  const allConns: Array<{
    gateUuid: string;
    pinName: string;
    pinDirection: string;
    wireUuid: string;
  }> = [];
  connectionsMap.forEach((yConn) => {
    allConns.push({
      gateUuid: yConn.get("gateId") as string,
      pinName: yConn.get("pinName") as string,
      pinDirection: yConn.get("pinDirection") as string,
      wireUuid: yConn.get("wireId") as string,
    });
  });

  // Group wires by shared pin: pinKey → wireUuids[]
  const pinToWires = new Map<string, string[]>();
  for (const conn of allConns) {
    const pinKey = `${conn.gateUuid}:${conn.pinName}:${conn.pinDirection}`;
    let arr = pinToWires.get(pinKey);
    if (!arr) {
      arr = [];
      pinToWires.set(pinKey, arr);
    }
    if (!arr.includes(conn.wireUuid)) arr.push(conn.wireUuid);
  }

  // Union-Find to merge wire groups transitively
  const parent = new Map<string, string>();
  function find(a: string): string {
    while (parent.get(a) !== a) {
      const p = parent.get(a)!;
      parent.set(a, parent.get(p)!);
      a = p;
    }
    return a;
  }
  function union(a: string, b: string) {
    const ra = find(a),
      rb = find(b);
    if (ra !== rb) parent.set(ra, rb);
  }

  wiresMap.forEach((_yWire, uuid) => {
    parent.set(uuid, uuid);
  });
  for (const conn of allConns) {
    if (!parent.has(conn.wireUuid)) parent.set(conn.wireUuid, conn.wireUuid);
  }
  for (const wireIds of pinToWires.values()) {
    for (let i = 1; i < wireIds.length; i++) {
      union(wireIds[0], wireIds[i]);
    }
  }

  // Build merged wire groups: canonical wireUuid → list of member wireUuids
  const wireGroups = new Map<string, string[]>();
  wiresMap.forEach((_yWire, uuid) => {
    const root = find(uuid);
    let group = wireGroups.get(root);
    if (!group) {
      group = [];
      wireGroups.set(root, group);
    }
    group.push(uuid);
  });

  // Assign sequential IDs to merged wires (one ID per group)
  const wireIdMap = new Map<string, number>(); // wireUuid → CDL int ID (all in same group share ID)
  wireGroups.forEach((members, root) => {
    const intId = nextId++;
    for (const uuid of members) {
      wireIdMap.set(uuid, intId);
    }
  });

  console.log(
    "[CDL export] Gates:",
    gateIdMap.size,
    "Wires:",
    wiresMap.size,
    "Merged wire groups:",
    wireGroups.size,
    "Connections:",
    allConns.length,
  );

  // Build gate connection index, deduplicated per pin (one entry per unique gate+pin)
  const gateConnections = new Map<
    string,
    Array<{ pinName: string; pinDirection: string; wireUuid: string }>
  >();

  const seenPins = new Set<string>();
  for (const conn of allConns) {
    const dedupKey = `${conn.gateUuid}:${conn.pinName}:${conn.pinDirection}`;
    if (seenPins.has(dedupKey)) continue;
    seenPins.add(dedupKey);

    let gArr = gateConnections.get(conn.gateUuid);
    if (!gArr) {
      gArr = [];
      gateConnections.set(conn.gateUuid, gArr);
    }
    gArr.push({
      pinName: conn.pinName,
      pinDirection: conn.pinDirection,
      wireUuid: conn.wireUuid,
    });
  }

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

    // Negate rotation back to CDL convention (Y-up, CCW)
    out += `<gparam>angle ${(-rotation).toFixed(1)}</gparam>\n`;

    // Look up gate definition to know which params are guiParams vs logic params
    const gateDef = loadedGateDefs.find((d) => d.id === defId);
    const guiParamKeys = new Set(Object.keys(gateDef?.guiParams ?? {}));

    for (const [k, v] of yGate.entries()) {
      if (k.startsWith("param:")) {
        const paramName = k.slice(6);
        if (guiParamKeys.has(paramName)) {
          out += `<gparam>${escapeXml(paramName)} ${escapeXml(String(v))}</gparam>\n`;
        } else {
          out += `<lparam>${escapeXml(paramName)} ${escapeXml(String(v))}</lparam>\n`;
        }
      }
    }

    out += `</gate>\n`;
  });

  // Wires — write merged wire groups, combining segment trees
  wireGroups.forEach((members, root) => {
    const intId = wireIdMap.get(root)!;

    out += `<wire>\n`;
    out += `<ID>${intId} </ID>\n`;
    out += `<shape>\n`;

    // Merge segment maps from all wires in the group, re-numbering segment IDs
    // to avoid collisions between different source wires.
    let nextSegId = 0;
    const segIdRemap = new Map<string, Map<number, number>>(); // wireUuid → old segId → new segId

    // First pass: assign new segment IDs
    for (const wireUuid of members) {
      const yWire = wiresMap.get(wireUuid);
      if (!yWire) continue;
      const model = readWireModel(yWire);
      if (!model) continue;
      const remap = new Map<number, number>();
      for (const seg of Object.values(model.segMap)) {
        remap.set(seg.id, nextSegId++);
      }
      segIdRemap.set(wireUuid, remap);
    }

    // Second pass: write segments with remapped IDs
    for (const wireUuid of members) {
      const yWire = wiresMap.get(wireUuid);
      if (!yWire) continue;
      const model = readWireModel(yWire);
      if (!model) continue;
      const remap = segIdRemap.get(wireUuid)!;

      for (const seg of Object.values(model.segMap)) {
        const segTag = seg.vertical ? "vsegment" : "hsegment";
        const [x1, y1] = toCdl(seg.begin.x, seg.begin.y);
        const [x2, y2] = toCdl(seg.end.x, seg.end.y);
        const newSegId = remap.get(seg.id)!;

        out += `<${segTag}>\n`;
        out += `<ID>${newSegId}</ID>\n`;
        out += `<points>${x1},${y1},${x2},${y2}</points>\n`;

        for (const conn of seg.connections) {
          const gateIntId = gateUuidToIntId.get(conn.gateId) ?? 0;
          out += `<connection>\n`;
          out += `<GID>${gateIntId}</GID>\n`;
          out += `<name>${escapeXml(conn.pinName)}</name>\n`;
          out += `</connection>\n`;
        }

        for (const [posStr, ids] of Object.entries(seg.intersects)) {
          const webPos = Number(posStr);
          const cdlPos = seg.vertical
            ? -webPos / GRID_SIZE
            : webPos / GRID_SIZE;
          for (const otherId of ids) {
            const remappedOtherId = remap.get(otherId) ?? otherId;
            out += `<intersection>${cdlPos} ${remappedOtherId}</intersection>\n`;
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
