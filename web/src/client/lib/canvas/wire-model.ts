/**
 * Pure-function wire model algorithms, ported from src/gui/guiWire.cpp.
 *
 * Every function operates on plain WireModel values — no Yjs, no Konva.
 * All mutations return new objects (shallow copies where appropriate).
 */

import type {
  WireConnection,
  WireSegmentNode,
  WireModel,
  WireRenderInfo,
} from "@shared/wire-types";
import { SNAP_SIZE } from "@shared/constants";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

type Point = { x: number; y: number };
type PinPosFn = (gateId: string, pinName: string) => Point;
type IsVerticalPinFn = (gateId: string, pinName: string) => boolean;

const EQUAL_RANGE = 0.001;

function snapToHalfGrid(v: number): number {
  return Math.round(v / SNAP_SIZE) * SNAP_SIZE;
}

function ptEq(a: Point, b: Point): boolean {
  return Math.abs(a.x - b.x) < EQUAL_RANGE && Math.abs(a.y - b.y) < EQUAL_RANGE;
}

function lineMagnitude(p1: Point, p2: Point): number {
  return Math.sqrt((p1.x - p2.x) ** 2 + (p1.y - p2.y) ** 2);
}

/** Distance from point p to line segment l1–l2. */
function distanceToLine(p: Point, l1: Point, l2: Point): number {
  const mag = lineMagnitude(l1, l2);
  if (mag < EQUAL_RANGE) return Infinity;
  const u =
    ((p.x - l1.x) * (l2.x - l1.x) + (p.y - l1.y) * (l2.y - l1.y)) / (mag * mag);
  if (u < EQUAL_RANGE || u > 1) {
    return Math.min(lineMagnitude(p, l1), lineMagnitude(p, l2));
  }
  return lineMagnitude(p, {
    x: l1.x + u * (l2.x - l1.x),
    y: l1.y + u * (l2.y - l1.y),
  });
}

function makeSeg(
  begin: Point,
  end: Point,
  vertical: boolean,
  id: number,
): WireSegmentNode {
  return {
    id,
    vertical,
    begin: { ...begin },
    end: { ...end },
    connections: [],
    intersects: {},
  };
}

/** Deep-clone a WireModel. */
export function cloneModel(w: WireModel): WireModel {
  const segMap: Record<number, WireSegmentNode> = {};
  for (const [k, seg] of Object.entries(w.segMap)) {
    const isects: Record<number, number[]> = {};
    for (const [ik, iv] of Object.entries(seg.intersects)) {
      isects[Number(ik)] = [...iv];
    }
    segMap[Number(k)] = {
      ...seg,
      begin: { ...seg.begin },
      end: { ...seg.end },
      connections: seg.connections.map((c) => ({ ...c })),
      intersects: isects,
    };
  }
  return { segMap, headSegment: w.headSegment, nextSegId: w.nextSegId };
}

/** Get all connections across all segments. */
function allConnections(w: WireModel): WireConnection[] {
  const result: WireConnection[] = [];
  for (const seg of Object.values(w.segMap)) {
    result.push(...seg.connections);
  }
  return result;
}

/** Sorted keys of an intersects map as numbers. */
function isectKeys(isects: Record<number, number[]>): number[] {
  return Object.keys(isects).map(Number).sort((a, b) => a - b);
}

function isectMin(isects: Record<number, number[]>): number {
  const keys = isectKeys(isects);
  return keys.length > 0 ? keys[0] : Infinity;
}

function isectMax(isects: Record<number, number[]>): number {
  const keys = isectKeys(isects);
  return keys.length > 0 ? keys[keys.length - 1] : -Infinity;
}

// ---------------------------------------------------------------------------
// createEmptyWire
// ---------------------------------------------------------------------------

export function createEmptyWire(): WireModel {
  return {
    segMap: {
      0: makeSeg({ x: 0, y: 0 }, { x: 0, y: 0 }, true, 0),
    },
    headSegment: 0,
    nextSegId: 1,
  };
}

// ---------------------------------------------------------------------------
// refreshIntersections — port of guiWire::refreshIntersections
// ---------------------------------------------------------------------------

export function refreshIntersections(
  w: WireModel,
  removeBadSegs = false,
): boolean {
  let retVal = false;
  for (const seg of Object.values(w.segMap)) {
    const refreshMap: Record<number, number[]> = {};
    for (const ids of Object.values(seg.intersects)) {
      for (const otherId of ids) {
        if (removeBadSegs && !(otherId in w.segMap)) {
          retVal = true;
          continue;
        }
        const other = w.segMap[otherId];
        const key = seg.vertical ? other.begin.y : other.begin.x;
        if (!refreshMap[key]) refreshMap[key] = [];
        refreshMap[key].push(otherId);
      }
    }
    seg.intersects = refreshMap;
  }
  return retVal;
}

// ---------------------------------------------------------------------------
// removeZeroLengthSegments — port of guiWire::removeZeroLengthSegments
// ---------------------------------------------------------------------------

export function removeZeroLengthSegments(w: WireModel): void {
  const segIds = Object.keys(w.segMap).map(Number);

  // Check if ALL segments are zero-length
  const allZero = segIds.every((id) => ptEq(w.segMap[id].begin, w.segMap[id].end));
  if (allZero) {
    const base = { ...w.segMap[w.headSegment] };
    const conns = allConnections(w);
    // Reset to single segment
    for (const id of segIds) delete w.segMap[id];
    w.headSegment = 0;
    w.segMap[0] = {
      ...base,
      id: 0,
      intersects: {},
      connections: conns,
    };
    w.nextSegId = 1;
    return;
  }

  const eraseIDs: number[] = [];
  let foundOne = false;

  for (const id of segIds) {
    const seg = w.segMap[id];
    if (!ptEq(seg.begin, seg.end)) continue;
    if (Object.keys(w.segMap).length === 2 && foundOne) break;
    foundOne = true;
    eraseIDs.push(id);

    // Transfer connections to the first non-zero-length intersecting segment
    const isectEntries = Object.entries(seg.intersects);
    let connectionsDone = false;
    let currentIsectIdx = 0;
    while (!connectionsDone && currentIsectIdx < isectEntries.length) {
      const ids = isectEntries[currentIsectIdx][1];
      for (const otherId of ids) {
        if (!connectionsDone && !ptEq(w.segMap[otherId].begin, w.segMap[otherId].end)) {
          w.segMap[otherId].connections.unshift(...seg.connections);
          connectionsDone = true;
        }
      }
      if (!connectionsDone) {
        // Follow the chain through zero-length segments
        const nextSeg = w.segMap[ids[0]];
        if (nextSeg) {
          const nextEntries = Object.entries(nextSeg.intersects);
          if (nextEntries.length > 0) {
            isectEntries.push(...nextEntries);
          }
        }
        currentIsectIdx++;
      }
    }
  }

  for (const id of eraseIDs) delete w.segMap[id];
  refreshIntersections(w, true);
  const remaining = Object.keys(w.segMap).map(Number);
  if (remaining.length > 0) {
    w.headSegment = Math.min(...remaining);
  }
}

// ---------------------------------------------------------------------------
// mergeSegments — port of guiWire::mergeSegments
// ---------------------------------------------------------------------------

export function mergeSegments(w: WireModel, getPinPos: PinPosFn): void {
  removeZeroLengthSegments(w);

  const newSegMap: Record<number, WireSegmentNode> = {};
  const mapIDs: Record<number, number> = {};

  for (const [idStr, cSegOrig] of Object.entries(w.segMap)) {
    const origId = Number(idStr);
    let cSeg = cSegOrig; // may be redirected to merged target
    let found = false;
    let mergingInMap = false;

    for (const [nIdStr, nSeg] of Object.entries(newSegMap)) {
      const nId = Number(nIdStr);
      if (cSeg.vertical !== nSeg.vertical) continue;

      // Check same channel
      if (cSeg.vertical && cSeg.begin.x !== nSeg.begin.x) continue;
      if (!cSeg.vertical && cSeg.begin.y !== nSeg.begin.y) continue;

      // Check overlap/touching
      let overlaps = false;
      if (cSeg.vertical) {
        overlaps =
          (cSeg.begin.y >= nSeg.begin.y - EQUAL_RANGE && cSeg.begin.y <= nSeg.end.y + EQUAL_RANGE) ||
          (cSeg.end.y >= nSeg.begin.y - EQUAL_RANGE && cSeg.end.y <= nSeg.end.y + EQUAL_RANGE) ||
          (nSeg.begin.y >= cSeg.begin.y - EQUAL_RANGE && nSeg.begin.y <= cSeg.end.y + EQUAL_RANGE) ||
          (nSeg.end.y >= cSeg.begin.y - EQUAL_RANGE && nSeg.end.y <= cSeg.end.y + EQUAL_RANGE);
      } else {
        overlaps =
          (cSeg.begin.x >= nSeg.begin.x - EQUAL_RANGE && cSeg.begin.x <= nSeg.end.x + EQUAL_RANGE) ||
          (cSeg.end.x >= nSeg.begin.x - EQUAL_RANGE && cSeg.end.x <= nSeg.end.x + EQUAL_RANGE) ||
          (nSeg.begin.x >= cSeg.begin.x - EQUAL_RANGE && nSeg.begin.x <= cSeg.end.x + EQUAL_RANGE) ||
          (nSeg.end.x >= cSeg.begin.x - EQUAL_RANGE && nSeg.end.x <= cSeg.end.x + EQUAL_RANGE);
      }

      if (!overlaps) continue;

      // Merge: combine connections and intersects
      nSeg.connections.push(...cSeg.connections);
      for (const [ik, iv] of Object.entries(cSeg.intersects)) {
        const key = Number(ik);
        if (!nSeg.intersects[key]) nSeg.intersects[key] = [];
        nSeg.intersects[key].push(...iv);
      }

      // Expand endpoints based on hotspots
      let hsMin = Infinity, hsMax = -Infinity;
      for (const conn of nSeg.connections) {
        const hp = getPinPos(conn.gateId, conn.pinName);
        if (cSeg.vertical) {
          hsMin = Math.min(hsMin, hp.y);
          hsMax = Math.max(hsMax, hp.y);
        } else {
          hsMin = Math.min(hsMin, hp.x);
          hsMax = Math.max(hsMax, hp.x);
        }
      }

      if (cSeg.vertical) {
        nSeg.begin.y = Math.min(hsMin, nSeg.begin.y);
        nSeg.begin.y = Math.min(nSeg.begin.y, isectMin(nSeg.intersects));
        nSeg.end.y = Math.max(hsMax, nSeg.end.y);
        nSeg.end.y = Math.max(nSeg.end.y, isectMax(nSeg.intersects));
      } else {
        nSeg.begin.x = Math.min(hsMin, nSeg.begin.x);
        nSeg.begin.x = Math.min(nSeg.begin.x, isectMin(nSeg.intersects));
        nSeg.end.x = Math.max(hsMax, nSeg.end.x);
        nSeg.end.x = Math.max(nSeg.end.x, isectMax(nSeg.intersects));
      }

      mapIDs[cSeg.id] = nSeg.id;
      if (mergingInMap) {
        delete newSegMap[cSeg.id];
        break; // merged twice, done
      }
      cSeg = nSeg;
      found = mergingInMap = true;
    }

    if (!found) {
      mapIDs[origId] = origId;
      newSegMap[origId] = cSeg;
    }
  }

  // Trim endpoints and remap intersection IDs
  const newIds = Object.keys(newSegMap).map(Number);
  if (newIds.length > 0) {
    w.headSegment = Math.min(...newIds);
  }

  for (const seg of Object.values(newSegMap)) {
    // Trim endpoints
    let hsMin = Infinity, hsMax = -Infinity;
    for (const conn of seg.connections) {
      const hp = getPinPos(conn.gateId, conn.pinName);
      if (seg.vertical) {
        hsMin = Math.min(hsMin, hp.y);
        hsMax = Math.max(hsMax, hp.y);
      } else {
        hsMin = Math.min(hsMin, hp.x);
        hsMax = Math.max(hsMax, hp.x);
      }
    }
    if (Object.keys(seg.intersects).length > 0) {
      hsMin = Math.min(hsMin, isectMin(seg.intersects));
      hsMax = Math.max(hsMax, isectMax(seg.intersects));
    }
    if (seg.vertical) {
      seg.begin.y = hsMin;
      seg.end.y = hsMax;
    } else {
      seg.begin.x = hsMin;
      seg.end.x = hsMax;
    }

    // Remap intersection segment IDs
    const newIntersects: Record<number, number[]> = {};
    for (const [ik, iv] of Object.entries(seg.intersects)) {
      const key = Number(ik);
      // Deduplicate
      const idSet = new Set(iv);
      const mapped: number[] = [];
      for (const oldId of idSet) {
        const newId = mapIDs[oldId] ?? oldId;
        // Only keep intersections with segments of different orientation
        if (newSegMap[newId] && newSegMap[newId].vertical !== seg.vertical) {
          mapped.push(newId);
        }
      }
      if (mapped.length > 0) newIntersects[key] = mapped;
    }
    seg.intersects = newIntersects;
  }

  w.segMap = newSegMap;
}

// ---------------------------------------------------------------------------
// calcShape — port of guiWire::calcShape
// ---------------------------------------------------------------------------

export function calcShape(
  connections: WireConnection[],
  getPinPos: PinPosFn,
  isVerticalPin: IsVerticalPinFn,
  existingSpinePos?: number | undefined,
): WireModel {
  if (connections.length < 2) {
    return createEmptyWire();
  }

  const vertices: Point[] = connections.map((c) => getPinPos(c.gateId, c.pinName));

  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  for (const v of vertices) {
    minX = Math.min(minX, v.x);
    maxX = Math.max(maxX, v.x);
    minY = Math.min(minY, v.y);
    maxY = Math.max(maxY, v.y);
  }

  const isOneVertical = isVerticalPin(connections[0].gateId, connections[0].pinName);
  const isTwoVertical = isVerticalPin(connections[1].gateId, connections[1].pinName);

  const w: WireModel = { segMap: {}, headSegment: 0, nextSegId: 1 };

  // CASE 1: Both horizontal hotspots (or fallout from both-vertical with same Y)
  if ((!isOneVertical && !isTwoVertical) || (isOneVertical && isTwoVertical && minY === maxY)) {
    let centerX: number;
    if (existingSpinePos !== undefined) {
      centerX = existingSpinePos;
    } else {
      centerX = snapToHalfGrid((minX + maxX) / 2);
    }

    // Vertical spine at segMap[0]
    w.segMap[0] = makeSeg({ x: centerX, y: minY }, { x: centerX, y: maxY }, true, 0);
    w.headSegment = 0;

    for (let i = connections.length - 1; i >= 0; i--) {
      const v = vertices[i];
      if (Math.abs(v.x - centerX) > EQUAL_RANGE) {
        const segId = w.nextSegId++;
        const bx = Math.min(v.x, centerX);
        const ex = Math.max(v.x, centerX);
        w.segMap[segId] = makeSeg({ x: bx, y: v.y }, { x: ex, y: v.y }, false, segId);
        w.segMap[segId].connections.push(connections[i]);
        w.segMap[segId].intersects[centerX] = [0];
        if (!w.segMap[0].intersects[v.y]) w.segMap[0].intersects[v.y] = [];
        w.segMap[0].intersects[v.y].push(segId);
      } else {
        // Connection lands on spine
        w.segMap[0].connections.push(connections[i]);
      }
    }
  }
  // CASE 2: Both vertical hotspots
  else if (isOneVertical && isTwoVertical) {
    let centerY: number;
    if (existingSpinePos !== undefined) {
      centerY = existingSpinePos;
    } else {
      centerY = snapToHalfGrid((minY + maxY) / 2);
    }

    // Horizontal spine at segMap[2]
    w.segMap[2] = makeSeg({ x: minX, y: centerY }, { x: maxX, y: centerY }, false, 2);
    w.headSegment = 2;
    let nextId = 0;

    for (let i = connections.length - 1; i >= 0; i--) {
      const v = vertices[i];
      if (Math.abs(v.y - centerY) > EQUAL_RANGE) {
        const segId = nextId++;
        const by = Math.min(v.y, centerY);
        const ey = Math.max(v.y, centerY);
        w.segMap[segId] = makeSeg({ x: v.x, y: by }, { x: v.x, y: ey }, true, segId);
        w.segMap[segId].connections.push(connections[i]);
        w.segMap[segId].intersects[centerY] = [2];
        if (!w.segMap[2].intersects[v.x]) w.segMap[2].intersects[v.x] = [];
        w.segMap[2].intersects[v.x].push(segId);
      } else {
        w.segMap[2].connections.push(connections[i]);
      }
    }

    w.nextSegId = 3;
  }
  // CASE 3: One vertical, one horizontal → L-shape
  else {
    const vertIdx = isOneVertical ? 0 : 1;
    const horizIdx = isOneVertical ? 1 : 0;
    const vv = vertices[vertIdx];
    const hv = vertices[horizIdx];

    w.segMap[0] = makeSeg(
      { x: vv.x, y: Math.min(vv.y, hv.y) },
      { x: vv.x, y: Math.max(vv.y, hv.y) },
      true, 0,
    );
    w.segMap[1] = makeSeg(
      { x: Math.min(vv.x, hv.x), y: hv.y },
      { x: Math.max(vv.x, hv.x), y: hv.y },
      false, 1,
    );
    w.segMap[0].connections.push(connections[vertIdx]);
    w.segMap[1].connections.push(connections[horizIdx]);
    w.segMap[0].intersects[hv.y] = [1];
    w.segMap[1].intersects[vv.x] = [0];
    w.nextSegId = 2;
    w.headSegment = 0;
  }

  // Merge to clean up, then return
  mergeSegments(w, getPinPos);
  return w;
}

// ---------------------------------------------------------------------------
// addConnection — port of guiWire::addConnection
// ---------------------------------------------------------------------------

export function addConnection(
  w: WireModel,
  connection: WireConnection,
  getPinPos: PinPosFn,
  isVerticalPin: IsVerticalPinFn,
): WireModel {
  const wire = cloneModel(w);
  const conns = allConnections(wire);
  conns.push(connection);

  if (conns.length < 3) {
    // Rebuild with calcShape
    return calcShape(conns, getPinPos, isVerticalPin);
  }

  const hsPoint = getPinPos(connection.gateId, connection.pinName);

  // Find nearest segment
  let minDist = Infinity;
  let closestId = wire.headSegment;
  for (const seg of Object.values(wire.segMap)) {
    const d = distanceToLine(hsPoint, seg.begin, seg.end);
    if (d < minDist) {
      minDist = d;
      closestId = seg.id;
    }
  }

  const closest = wire.segMap[closestId];
  // Create perpendicular stub
  if (!closest.vertical) {
    // Closest is horizontal → create vertical stub
    if (ptEq(closest.begin, closest.end)) closest.end.x += 1;
    const by = Math.min(hsPoint.y, closest.begin.y);
    const ey = Math.max(hsPoint.y, closest.begin.y);
    const newId = wire.nextSegId++;
    wire.segMap[newId] = makeSeg({ x: hsPoint.x, y: by }, { x: hsPoint.x, y: ey }, true, newId);
    if (!closest.intersects[hsPoint.x]) closest.intersects[hsPoint.x] = [];
    closest.intersects[hsPoint.x].push(newId);
    wire.segMap[newId].intersects[closest.begin.y] = [closestId];
  } else {
    // Closest is vertical → create horizontal stub
    if (ptEq(closest.begin, closest.end)) closest.end.y += 1;
    const bx = Math.min(hsPoint.x, closest.begin.x);
    const ex = Math.max(hsPoint.x, closest.begin.x);
    const newId = wire.nextSegId++;
    wire.segMap[newId] = makeSeg({ x: bx, y: hsPoint.y }, { x: ex, y: hsPoint.y }, false, newId);
    if (!closest.intersects[hsPoint.y]) closest.intersects[hsPoint.y] = [];
    closest.intersects[hsPoint.y].push(newId);
    wire.segMap[newId].intersects[closest.begin.x] = [closestId];
  }

  wire.segMap[wire.nextSegId - 1].connections.push(connection);
  mergeSegments(wire, getPinPos);
  return wire;
}

// ---------------------------------------------------------------------------
// removeConnection — port of guiWire::removeConnection
// ---------------------------------------------------------------------------

export function removeConnection(
  w: WireModel,
  gateId: string,
  pinName: string,
  getPinPos: PinPosFn,
): WireModel {
  const wire = cloneModel(w);

  // Find the segment with this connection
  let segID = -1;
  for (const seg of Object.values(wire.segMap)) {
    const idx = seg.connections.findIndex(
      (c) => c.gateId === gateId && c.pinName === pinName,
    );
    if (idx !== -1) {
      seg.connections.splice(idx, 1);
      segID = seg.id;
      break;
    }
  }
  if (segID === -1) return wire;

  // Walk back trimming empty branches
  while (
    wire.segMap[segID] &&
    wire.segMap[segID].connections.length === 0 &&
    Object.keys(wire.segMap[segID].intersects).length === 1
  ) {
    const oldSegID = segID;
    const isectValues = Object.values(wire.segMap[oldSegID].intersects);
    segID = isectValues[0][0];
    const otherSeg = wire.segMap[segID];
    if (!otherSeg) break;

    const mapKey = otherSeg.vertical
      ? wire.segMap[oldSegID].begin.y
      : wire.segMap[oldSegID].begin.x;

    if (otherSeg.intersects[mapKey]) {
      otherSeg.intersects[mapKey] = otherSeg.intersects[mapKey].filter(
        (id) => id !== oldSegID,
      );
      if (otherSeg.intersects[mapKey].length === 0) {
        delete otherSeg.intersects[mapKey];
      }
    }
    delete wire.segMap[oldSegID];
  }

  mergeSegments(wire, getPinPos);
  return wire;
}

// ---------------------------------------------------------------------------
// startSegDrag — port of guiWire::startSegDrag
// ---------------------------------------------------------------------------

export interface DragState {
  wire: WireModel;
  oldSegMap: Record<number, WireSegmentNode>;
  dragSegId: number;
}

export function startSegDrag(
  w: WireModel,
  segId: number,
  getPinPos: PinPosFn,
): DragState {
  const wire = cloneModel(w);
  const oldSegMap = cloneModel(w).segMap;
  const seg = wire.segMap[segId];
  if (!seg) return { wire, oldSegMap, dragSegId: -1 };

  // Create zero-length perpendicular stubs for each connection on the drag segment
  const newSegs: WireSegmentNode[] = [];
  for (const conn of seg.connections) {
    const vertex = getPinPos(conn.gateId, conn.pinName);
    const newId = wire.nextSegId++;
    if (seg.vertical) {
      const stub = makeSeg(vertex, { ...vertex }, false, newId);
      stub.intersects[vertex.x] = [seg.id];
      stub.connections.push(conn);
      if (!seg.intersects[vertex.y]) seg.intersects[vertex.y] = [];
      seg.intersects[vertex.y].push(newId);
      newSegs.push(stub);
    } else {
      const stub = makeSeg(vertex, { ...vertex }, true, newId);
      stub.intersects[vertex.y] = [seg.id];
      stub.connections.push(conn);
      if (!seg.intersects[vertex.x]) seg.intersects[vertex.x] = [];
      seg.intersects[vertex.x].push(newId);
      newSegs.push(stub);
    }
  }
  seg.connections = [];
  for (const ns of newSegs) {
    wire.segMap[ns.id] = ns;
  }

  return { wire, oldSegMap, dragSegId: segId };
}

// ---------------------------------------------------------------------------
// updateSegDrag — port of guiWire::updateSegDrag
// ---------------------------------------------------------------------------

export function updateSegDrag(
  state: DragState,
  delta: number,
  getPinPos: PinPosFn,
): DragState {
  if (state.dragSegId === -1) return state;
  const wire = cloneModel(state.wire);
  const seg = wire.segMap[state.dragSegId];
  if (!seg) return state;

  // Move the dragged segment perpendicular to its orientation
  if (seg.vertical) {
    seg.begin.x += delta;
    seg.end.x += delta;
  } else {
    seg.begin.y += delta;
    seg.end.y += delta;
  }

  refreshIntersections(wire);

  // Update neighbors
  for (const ids of Object.values(seg.intersects)) {
    for (const otherId of ids) {
      const ws = wire.segMap[otherId];
      if (!ws) continue;

      let hsMin = Infinity, hsMax = -Infinity;

      if (seg.vertical) {
        // Dragged seg is vertical; neighbors are horizontal → adjust x endpoints
        for (const conn of ws.connections) {
          const hp = getPinPos(conn.gateId, conn.pinName);
          hsMin = Math.min(hsMin, hp.x);
          hsMax = Math.max(hsMax, hp.x);
        }
        const iLeft = isectMin(ws.intersects);
        const iRight = isectMax(ws.intersects);
        ws.begin.x = Math.min(seg.begin.x, hsMin, iLeft);
        ws.end.x = Math.max(seg.begin.x, hsMax, iRight);
      } else {
        // Dragged seg is horizontal; neighbors are vertical → adjust y endpoints
        for (const conn of ws.connections) {
          const hp = getPinPos(conn.gateId, conn.pinName);
          hsMin = Math.min(hsMin, hp.y);
          hsMax = Math.max(hsMax, hp.y);
        }
        const iBot = isectMin(ws.intersects);
        const iTop = isectMax(ws.intersects);
        ws.begin.y = Math.min(seg.begin.y, hsMin, iBot);
        ws.end.y = Math.max(seg.begin.y, hsMax, iTop);
      }
    }
  }

  refreshIntersections(wire);

  return { ...state, wire };
}

// ---------------------------------------------------------------------------
// endSegDrag — port of guiWire::endSegDrag
// ---------------------------------------------------------------------------

export function endSegDrag(state: DragState, getPinPos: PinPosFn): WireModel {
  const wire = cloneModel(state.wire);
  mergeSegments(wire, getPinPos);
  return wire;
}

// ---------------------------------------------------------------------------
// updateConnectionPos — port of guiWire::updateConnectionPos
// ---------------------------------------------------------------------------

export function updateConnectionPos(
  w: WireModel,
  gateId: string,
  pinName: string,
  getPinPos: PinPosFn,
  isVerticalPin: IsVerticalPinFn,
): WireModel {
  const wire = cloneModel(w);
  const isVertical = isVerticalPin(gateId, pinName);
  const newLocation = getPinPos(gateId, pinName);

  // Find the segment with this connection
  let currentDragSegment = -1;
  let connID = -1;
  for (const seg of Object.values(wire.segMap)) {
    for (let j = 0; j < seg.connections.length; j++) {
      if (seg.connections[j].gateId === gateId && seg.connections[j].pinName === pinName) {
        currentDragSegment = seg.id;
        connID = j;
        break;
      }
    }
    if (currentDragSegment !== -1) break;
  }
  if (currentDragSegment === -1) return wire;

  if (!isVertical) {
    // Horizontal hotspot
    const seg = wire.segMap[currentDragSegment];
    if (seg.vertical) {
      // Create horizontal stub
      const newId = wire.nextSegId++;
      wire.segMap[newId] = makeSeg(
        { ...newLocation },
        { x: seg.begin.x, y: newLocation.y },
        false, newId,
      );
      wire.segMap[newId].intersects[seg.begin.x] = [currentDragSegment];
      wire.segMap[newId].connections.push(seg.connections[connID]);
      if (!seg.intersects[newLocation.y]) seg.intersects[newLocation.y] = [];
      seg.intersects[newLocation.y].push(newId);
      seg.connections.splice(connID, 1);
      currentDragSegment = newId;
      connID = 0;
    }

    const dragSeg = wire.segMap[currentDragSegment];
    // Create stubs for other connections on the segment
    for (let j = 0; j < dragSeg.connections.length; j++) {
      if (j !== connID) {
        const connPoint = getPinPos(dragSeg.connections[j].gateId, dragSeg.connections[j].pinName);
        const newId = wire.nextSegId++;
        wire.segMap[newId] = makeSeg({ ...connPoint }, { ...connPoint }, true, newId);
        wire.segMap[newId].intersects[connPoint.y] = [currentDragSegment];
        wire.segMap[newId].connections.push(dragSeg.connections[j]);
        if (!dragSeg.intersects[connPoint.x]) dragSeg.intersects[connPoint.x] = [];
        dragSeg.intersects[connPoint.x].push(newId);
      }
    }

    // Keep only the moved connection
    const wc = dragSeg.connections[connID];
    dragSeg.connections = [wc];

    // Resize segment endpoints
    const iLeft = isectMin(dragSeg.intersects);
    const iRight = isectMax(dragSeg.intersects);
    const prevY = dragSeg.begin.y;
    dragSeg.begin.x = Math.min(newLocation.x, iLeft);
    dragSeg.end.x = Math.max(newLocation.x, iRight);

    // Simulate updateSegDrag for the vertical shift
    const ds: DragState = { wire, oldSegMap: {}, dragSegId: currentDragSegment };
    const result = updateSegDrag(ds, newLocation.y - prevY, getPinPos);
    return endSegDrag({ ...result, dragSegId: -1 }, getPinPos);
  } else {
    // Vertical hotspot
    const seg = wire.segMap[currentDragSegment];
    if (!seg.vertical) {
      // Create vertical stub
      const newId = wire.nextSegId++;
      wire.segMap[newId] = makeSeg(
        { ...newLocation },
        { x: newLocation.x, y: seg.begin.y },
        true, newId,
      );
      wire.segMap[newId].intersects[seg.begin.y] = [currentDragSegment];
      wire.segMap[newId].connections.push(seg.connections[connID]);
      if (!seg.intersects[newLocation.x]) seg.intersects[newLocation.x] = [];
      seg.intersects[newLocation.x].push(newId);
      seg.connections.splice(connID, 1);
      currentDragSegment = newId;
      connID = 0;
    }

    const dragSeg = wire.segMap[currentDragSegment];
    // Create stubs for other connections
    for (let j = 0; j < dragSeg.connections.length; j++) {
      if (j !== connID) {
        const connPoint = getPinPos(dragSeg.connections[j].gateId, dragSeg.connections[j].pinName);
        const newId = wire.nextSegId++;
        wire.segMap[newId] = makeSeg({ ...connPoint }, { ...connPoint }, false, newId);
        wire.segMap[newId].intersects[connPoint.x] = [currentDragSegment];
        wire.segMap[newId].connections.push(dragSeg.connections[j]);
        if (!dragSeg.intersects[connPoint.y]) dragSeg.intersects[connPoint.y] = [];
        dragSeg.intersects[connPoint.y].push(newId);
      }
    }

    const wc = dragSeg.connections[connID];
    dragSeg.connections = [wc];

    const iBot = isectMin(dragSeg.intersects);
    const iTop = isectMax(dragSeg.intersects);
    const prevX = dragSeg.begin.x;
    dragSeg.begin.y = Math.min(newLocation.y, iBot);
    dragSeg.end.y = Math.max(newLocation.y, iTop);

    const ds: DragState = { wire, oldSegMap: {}, dragSegId: currentDragSegment };
    const result = updateSegDrag(ds, newLocation.x - prevX, getPinPos);
    return endSegDrag({ ...result, dragSegId: -1 }, getPinPos);
  }
}

// ---------------------------------------------------------------------------
// generateRenderInfo — port of guiWire::generateRenderInfo
// ---------------------------------------------------------------------------

export function generateRenderInfo(
  w: WireModel,
  getPinPos?: PinPosFn,
): WireRenderInfo {
  const info: WireRenderInfo = {
    lineSegments: [],
    intersectPoints: [],
    vertexPoints: [],
  };

  // Gate connection points
  if (getPinPos) {
    for (const seg of Object.values(w.segMap)) {
      for (const conn of seg.connections) {
        const p = getPinPos(conn.gateId, conn.pinName);
        info.vertexPoints.push(p);
      }
    }
  }

  // Lines and intersections
  for (const seg of Object.values(w.segMap)) {
    info.lineSegments.push({
      x1: seg.begin.x, y1: seg.begin.y,
      x2: seg.end.x, y2: seg.end.y,
    });

    // T-junction dots (skip elbows at segment endpoints)
    for (const [posStr, ids] of Object.entries(seg.intersects)) {
      const pos = Number(posStr);
      if (seg.vertical) {
        if (Math.abs(pos - seg.begin.y) < EQUAL_RANGE || Math.abs(pos - seg.end.y) < EQUAL_RANGE) continue;
      } else {
        if (Math.abs(pos - seg.begin.x) < EQUAL_RANGE || Math.abs(pos - seg.end.x) < EQUAL_RANGE) continue;
      }
      for (let i = 0; i < ids.length; i++) {
        const x = seg.vertical ? seg.begin.x : pos;
        const y = seg.vertical ? pos : seg.begin.y;
        info.intersectPoints.push({ x, y });
      }
    }
  }

  return info;
}
