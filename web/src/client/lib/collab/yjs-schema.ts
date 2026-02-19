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

export interface YGate {
  defId: string;
  logicType: string;
  x: number;
  y: number;
  rotation: number;
  [key: `param:${string}`]: string;
}

export interface YWire {
  segments: string; // JSON array of {x1,y1,x2,y2}
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
