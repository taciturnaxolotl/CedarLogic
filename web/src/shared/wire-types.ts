/** Wire connection: links a segment to a gate pin. */
export interface WireConnection {
  gateId: string;
  pinName: string;
  pinDirection: "input" | "output";
}

/**
 * A single wire segment node in the segment tree.
 * Segments are always axis-aligned (vertical or horizontal).
 * `begin` is always the lesser coordinate; `end` is always the greater.
 */
export interface WireSegmentNode {
  id: number;
  vertical: boolean;
  begin: { x: number; y: number };
  end: { x: number; y: number };
  connections: WireConnection[];
  /** Intersection map: position → array of crossing segment IDs.
   *  For horizontal segs the key is x; for vertical segs the key is y. */
  intersects: Record<number, number[]>;
}

/** The complete wire model — a segment tree with metadata. */
export interface WireModel {
  segMap: Record<number, WireSegmentNode>;
  headSegment: number;
  nextSegId: number;
}

/** Pre-computed render data for a wire. */
export interface WireRenderInfo {
  lineSegments: Array<{ x1: number; y1: number; x2: number; y2: number }>;
  intersectPoints: Array<{ x: number; y: number }>;
  vertexPoints: Array<{ x: number; y: number }>;
}
