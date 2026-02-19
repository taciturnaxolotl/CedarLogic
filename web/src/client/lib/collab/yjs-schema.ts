/**
 * Yjs document schema for CedarLogic circuits.
 *
 * Flat Y.Map-of-Y.Maps pattern:
 * - doc.getMap("gates")       — keyed by gate ID string
 * - doc.getMap("wires")       — keyed by wire ID string
 * - doc.getMap("connections")  — keyed by "g{gateId}:{pinName}:{wireId}"
 * - doc.getMap("meta")        — title, simSpeed, simRunning
 *
 * Wire states are NOT stored in Yjs — they're derived locally from simulation.
 */
import * as Y from "yjs";
import type { WireModel } from "@shared/wire-types";

export interface YGate {
  defId: string;
  logicType: string;
  x: number;
  y: number;
  rotation: number;
  [key: `param:${string}`]: string;
}

export interface YWire {
  segments?: string; // Legacy: JSON array of {x1,y1,x2,y2}
  model?: string;    // New: JSON of WireModel
}

export interface YConnection {
  gateId: string;
  pinName: string;
  pinDirection: "input" | "output";
  wireId: string;
}

export interface YMeta {
  title: string;
  simSpeed: number;
  simRunning: boolean;
}

export function getGatesMap(doc: Y.Doc): Y.Map<Y.Map<any>> {
  return doc.getMap("gates") as Y.Map<Y.Map<any>>;
}

export function getWiresMap(doc: Y.Doc): Y.Map<Y.Map<any>> {
  return doc.getMap("wires") as Y.Map<Y.Map<any>>;
}

export function getConnectionsMap(doc: Y.Doc): Y.Map<Y.Map<any>> {
  return doc.getMap("connections") as Y.Map<Y.Map<any>>;
}

export function getMetaMap(doc: Y.Doc): Y.Map<any> {
  return doc.getMap("meta");
}

export function addGateToDoc(
  doc: Y.Doc,
  id: string,
  gate: YGate
): void {
  const gates = getGatesMap(doc);
  const yGate = new Y.Map<any>();
  yGate.set("defId", gate.defId);
  yGate.set("logicType", gate.logicType);
  yGate.set("x", gate.x);
  yGate.set("y", gate.y);
  yGate.set("rotation", gate.rotation);
  for (const [k, v] of Object.entries(gate)) {
    if (k.startsWith("param:")) yGate.set(k, v);
  }
  gates.set(id, yGate);
}

/** @deprecated Use addWireModelToDoc instead. Kept for backward compat. */
export function addWireToDoc(
  doc: Y.Doc,
  id: string,
  segments: Array<{ x1: number; y1: number; x2: number; y2: number }>
): void {
  const wires = getWiresMap(doc);
  const yWire = new Y.Map<any>();
  yWire.set("segments", JSON.stringify(segments));
  wires.set(id, yWire);
}

/** Write a WireModel to the Yjs document. */
export function addWireModelToDoc(
  doc: Y.Doc,
  id: string,
  wireModel: WireModel,
): void {
  const wires = getWiresMap(doc);
  const yWire = new Y.Map<any>();
  yWire.set("model", JSON.stringify(wireModel));
  wires.set(id, yWire);
}

/** Update an existing wire's model in Yjs. */
export function updateWireModel(
  doc: Y.Doc,
  wireId: string,
  wireModel: WireModel,
): void {
  const wires = getWiresMap(doc);
  const yWire = wires.get(wireId);
  if (!yWire) return;
  yWire.set("model", JSON.stringify(wireModel));
  // Remove legacy key if present
  if (yWire.get("segments") !== undefined) {
    yWire.delete("segments");
  }
}

/**
 * Read a WireModel from Yjs, handling migration from old format.
 * Old format: `segments` key with JSON array of {x1,y1,x2,y2}.
 * New format: `model` key with JSON WireModel.
 */
export function readWireModel(yWire: Y.Map<any>): WireModel | null {
  // New format
  const modelStr = yWire.get("model");
  if (modelStr) {
    try {
      return JSON.parse(modelStr) as WireModel;
    } catch {
      return null;
    }
  }

  // Old format — migrate flat segments to a simple wire model
  const segStr = yWire.get("segments");
  if (segStr) {
    try {
      const segs: Array<{ x1: number; y1: number; x2: number; y2: number }> =
        JSON.parse(segStr);
      return migrateOldSegments(segs);
    } catch {
      return null;
    }
  }

  return null;
}

/**
 * Convert old flat segment array to a WireModel.
 * Creates a simple chain of segments with intersections between adjacent pairs.
 */
function migrateOldSegments(
  segs: Array<{ x1: number; y1: number; x2: number; y2: number }>,
): WireModel {
  const w: WireModel = { segMap: {}, headSegment: 0, nextSegId: 0 };
  for (let i = 0; i < segs.length; i++) {
    const s = segs[i];
    const isVert = s.x1 === s.x2;
    w.segMap[i] = {
      id: i,
      vertical: isVert,
      begin: { x: Math.min(s.x1, s.x2), y: Math.min(s.y1, s.y2) },
      end: { x: Math.max(s.x1, s.x2), y: Math.max(s.y1, s.y2) },
      connections: [],
      intersects: {},
    };
  }

  // Build intersections between adjacent segments
  for (let i = 0; i < segs.length - 1; i++) {
    const a = w.segMap[i];
    const b = w.segMap[i + 1];
    if (a.vertical !== b.vertical) {
      // Adjacent perpendicular segments intersect at their meeting point
      if (a.vertical) {
        // a is vertical, b is horizontal — they meet at (a.begin.x, b.begin.y)
        const key = b.begin.y;
        if (!a.intersects[key]) a.intersects[key] = [];
        a.intersects[key].push(b.id);
        const bKey = a.begin.x;
        if (!b.intersects[bKey]) b.intersects[bKey] = [];
        b.intersects[bKey].push(a.id);
      } else {
        // a is horizontal, b is vertical
        const key = b.begin.x;
        if (!a.intersects[key]) a.intersects[key] = [];
        a.intersects[key].push(b.id);
        const bKey = a.begin.y;
        if (!b.intersects[bKey]) b.intersects[bKey] = [];
        b.intersects[bKey].push(a.id);
      }
    }
  }

  w.nextSegId = segs.length;
  w.headSegment = 0;
  return w;
}

/**
 * Rebuild the Yjs `connections` map entries from a wire's segment tree.
 * This keeps the simulation bridge (which reads from the connections map) working.
 */
export function syncConnectionsFromWire(
  doc: Y.Doc,
  wireId: string,
  wireModel: WireModel,
): void {
  const connections = getConnectionsMap(doc);

  // Remove existing connections for this wire
  const keysToDelete: string[] = [];
  connections.forEach((_yConn, key) => {
    if (key.includes(`:${wireId}`)) {
      keysToDelete.push(key);
    }
  });
  for (const key of keysToDelete) {
    connections.delete(key);
  }

  // Add connections from the segment tree
  for (const seg of Object.values(wireModel.segMap)) {
    for (const conn of seg.connections) {
      addConnectionToDoc(doc, conn.gateId, conn.pinName, conn.pinDirection, wireId);
    }
  }
}

export function addConnectionToDoc(
  doc: Y.Doc,
  gateId: string,
  pinName: string,
  pinDirection: "input" | "output",
  wireId: string
): void {
  const connections = getConnectionsMap(doc);
  const key = `g${gateId}:${pinName}:${wireId}`;
  const yConn = new Y.Map<any>();
  yConn.set("gateId", gateId);
  yConn.set("pinName", pinName);
  yConn.set("pinDirection", pinDirection);
  yConn.set("wireId", wireId);
  connections.set(key, yConn);
}
