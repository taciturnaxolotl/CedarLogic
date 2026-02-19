import React, { useEffect, useState, useCallback, useRef } from "react";
import { Layer, Line, Rect } from "react-konva";
import * as Y from "yjs";
import type Konva from "konva";
import { getWiresMap } from "../../lib/collab/yjs-schema";
import { useSimulationStore } from "../../stores/simulation-store";
import { useCanvasStore } from "../../stores/canvas-store";
import { WIRE_COLORS, WIRE_STATE, SNAP_SIZE } from "@shared/constants";
import type { WireSegment } from "@shared/types";

interface WireLayerProps {
  doc: Y.Doc;
  readOnly: boolean;
}

interface WireRenderData {
  id: string;
  segments: WireSegment[];
}

function snapToGrid(val: number): number {
  return Math.round(val / SNAP_SIZE) * SNAP_SIZE;
}

function isHorizontal(seg: WireSegment): boolean {
  return seg.y1 === seg.y2;
}

/** Remove zero-length segments and merge collinear adjacent segments. */
function cleanSegments(segments: WireSegment[]): WireSegment[] {
  // Remove zero-length segments (point → point)
  let result = segments.filter((s) => !(s.x1 === s.x2 && s.y1 === s.y2));

  // Merge collinear adjacent segments
  let changed = true;
  while (changed) {
    changed = false;
    for (let i = 0; i < result.length - 1; i++) {
      const a = result[i];
      const b = result[i + 1];
      // Both horizontal at same Y
      if (a.y1 === a.y2 && b.y1 === b.y2 && a.y2 === b.y1) {
        result[i] = { x1: a.x1, y1: a.y1, x2: b.x2, y2: a.y1 };
        result.splice(i + 1, 1);
        changed = true;
        break;
      }
      // Both vertical at same X
      if (a.x1 === a.x2 && b.x1 === b.x2 && a.x2 === b.x1) {
        result[i] = { x1: a.x1, y1: a.y1, x2: a.x1, y2: b.y2 };
        result.splice(i + 1, 1);
        changed = true;
        break;
      }
    }
  }

  return result;
}

export function WireLayer({ doc, readOnly }: WireLayerProps) {
  const [wires, setWires] = useState<Map<string, WireRenderData>>(new Map());
  const wireStates = useSimulationStore((s) => s.wireStates);
  const selectedIds = useCanvasStore((s) => s.selectedIds);
  const selectOnly = useCanvasStore((s) => s.selectOnly);

  // Snapshot of segments at drag start — prevents cascading bridge insertions
  const dragOriginalRef = useRef<Map<string, WireSegment[]>>(new Map());

  useEffect(() => {
    const wiresMap = getWiresMap(doc);

    function sync() {
      const next = new Map<string, WireRenderData>();
      wiresMap.forEach((yWire, id) => {
        try {
          const segments = JSON.parse(yWire.get("segments") || "[]");
          next.set(id, { id, segments });
        } catch {
          // Skip malformed wires
        }
      });
      setWires(next);
    }

    sync();
    wiresMap.observeDeep(sync);
    return () => wiresMap.unobserveDeep(sync);
  }, [doc]);

  const handleSegmentDragStart = useCallback(
    (wireId: string) => {
      if (readOnly) return;
      const wiresMap = getWiresMap(doc);
      const yWire = wiresMap.get(wireId);
      if (!yWire) return;
      try {
        const segments: WireSegment[] = JSON.parse(yWire.get("segments") || "[]");
        dragOriginalRef.current.set(wireId, segments);
      } catch {
        // ignore
      }
    },
    [doc, readOnly],
  );

  const handleSegmentDrag = useCallback(
    (wireId: string, segIndex: number, e: Konva.KonvaEventObject<DragEvent>) => {
      if (readOnly) return;
      const wiresMap = getWiresMap(doc);
      const yWire = wiresMap.get(wireId);
      if (!yWire) return;

      const original = dragOriginalRef.current.get(wireId);
      if (!original) return;
      if (segIndex < 0 || segIndex >= original.length) return;

      // Always compute from the original snapshot (deep clone)
      const segments: WireSegment[] = original.map((s) => ({ ...s }));
      const seg = segments[segIndex];
      const horiz = isHorizontal(seg);
      const isFirst = segIndex === 0;
      const isLast = segIndex === original.length - 1;

      if (isFirst) {
        // First segment — pin is at (seg.x1, seg.y1), insert bridge to keep pin connected
        const pinX = seg.x1, pinY = seg.y1;
        if (horiz) {
          const newY = snapToGrid(e.target.y());
          const bridge: WireSegment = { x1: pinX, y1: pinY, x2: pinX, y2: newY };
          seg.y1 = newY;
          seg.y2 = newY;
          if (segments.length > 1) segments[1].y1 = newY;
          segments.splice(0, 0, bridge);
        } else {
          const newX = snapToGrid(e.target.x());
          const bridge: WireSegment = { x1: pinX, y1: pinY, x2: newX, y2: pinY };
          seg.x1 = newX;
          seg.x2 = newX;
          if (segments.length > 1) segments[1].x1 = newX;
          segments.splice(0, 0, bridge);
        }
      } else if (isLast) {
        // Last segment — pin is at (seg.x2, seg.y2), insert bridge to keep pin connected
        const pinX = seg.x2, pinY = seg.y2;
        if (horiz) {
          const newY = snapToGrid(e.target.y());
          const bridge: WireSegment = { x1: pinX, y1: newY, x2: pinX, y2: pinY };
          seg.y1 = newY;
          seg.y2 = newY;
          if (segments.length > 1) segments[segments.length - 2].y2 = newY;
          segments.push(bridge);
        } else {
          const newX = snapToGrid(e.target.x());
          const bridge: WireSegment = { x1: newX, y1: pinY, x2: pinX, y2: pinY };
          seg.x1 = newX;
          seg.x2 = newX;
          if (segments.length > 1) segments[segments.length - 2].x2 = newX;
          segments.push(bridge);
        }
      } else {
        // Interior segment — elastic stretch neighbors
        if (horiz) {
          const newY = snapToGrid(e.target.y());
          seg.y1 = newY;
          seg.y2 = newY;
          segments[segIndex - 1].y2 = newY;
          segments[segIndex + 1].y1 = newY;
        } else {
          const newX = snapToGrid(e.target.x());
          seg.x1 = newX;
          seg.x2 = newX;
          segments[segIndex - 1].x2 = newX;
          segments[segIndex + 1].x1 = newX;
        }
      }

      // Clean up: remove zero-length segments, merge collinear neighbors
      yWire.set("segments", JSON.stringify(cleanSegments(segments)));
    },
    [doc, readOnly],
  );

  const handleSegmentDragEnd = useCallback(
    (wireId: string, segIndex: number, e: Konva.KonvaEventObject<DragEvent>) => {
      handleSegmentDrag(wireId, segIndex, e);
      dragOriginalRef.current.delete(wireId);
    },
    [handleSegmentDrag],
  );

  const handleWireClick = useCallback(
    (wireId: string, e: Konva.KonvaEventObject<MouseEvent>) => {
      e.cancelBubble = true;
      if (e.evt.button !== 0) return;
      selectOnly(wireId);
    },
    [selectOnly],
  );

  return (
    <Layer>
      {Array.from(wires.values()).map((wire) => {
        const state = wireStates.get(wire.id) ?? WIRE_STATE.UNKNOWN;
        const color = WIRE_COLORS[state];
        const selected = !!selectedIds[wire.id];
        const strokeColor = selected ? "#3b82f6" : color;

        // Render wire segments as a single polyline
        const points: number[] = [];
        if (wire.segments.length > 0) {
          points.push(wire.segments[0].x1, wire.segments[0].y1);
          for (const seg of wire.segments) {
            points.push(seg.x2, seg.y2);
          }
        }

        return (
          <React.Fragment key={wire.id}>
            {/* Visible wire */}
            <Line
              points={points}
              stroke={strokeColor}
              strokeWidth={2}
              lineCap="round"
              lineJoin="round"
              hitStrokeWidth={10}
              onMouseDown={(e) => handleWireClick(wire.id, e)}
            />
            {/* Draggable handles for every segment */}
            {!readOnly && wire.segments.map((seg, i) => {
              const horiz = isHorizontal(seg);

              if (horiz) {
                const minX = Math.min(seg.x1, seg.x2);
                const segWidth = Math.abs(seg.x2 - seg.x1);
                return (
                  <Rect
                    key={`drag-${i}`}
                    x={minX}
                    y={seg.y1}
                    width={segWidth}
                    height={0}
                    stroke="transparent"
                    strokeWidth={12}
                    hitStrokeWidth={12}
                    draggable
                    dragBoundFunc={(pos) => ({
                      x: minX,
                      y: pos.y,
                    })}
                    onMouseDown={(e) => handleWireClick(wire.id, e)}
                    onDragStart={() => handleSegmentDragStart(wire.id)}
                    onDragMove={(e) => handleSegmentDrag(wire.id, i, e)}
                    onDragEnd={(e) => handleSegmentDragEnd(wire.id, i, e)}
                    onMouseEnter={(e) => {
                      const container = e.target.getStage()?.container();
                      if (container) container.style.cursor = "row-resize";
                    }}
                    onMouseLeave={(e) => {
                      const container = e.target.getStage()?.container();
                      if (container) container.style.cursor = "";
                    }}
                  />
                );
              } else {
                const minY = Math.min(seg.y1, seg.y2);
                const segHeight = Math.abs(seg.y2 - seg.y1);
                return (
                  <Rect
                    key={`drag-${i}`}
                    x={seg.x1}
                    y={minY}
                    width={0}
                    height={segHeight}
                    stroke="transparent"
                    strokeWidth={12}
                    hitStrokeWidth={12}
                    draggable
                    dragBoundFunc={(pos) => ({
                      x: pos.x,
                      y: minY,
                    })}
                    onMouseDown={(e) => handleWireClick(wire.id, e)}
                    onDragStart={() => handleSegmentDragStart(wire.id)}
                    onDragMove={(e) => handleSegmentDrag(wire.id, i, e)}
                    onDragEnd={(e) => handleSegmentDragEnd(wire.id, i, e)}
                    onMouseEnter={(e) => {
                      const container = e.target.getStage()?.container();
                      if (container) container.style.cursor = "col-resize";
                    }}
                    onMouseLeave={(e) => {
                      const container = e.target.getStage()?.container();
                      if (container) container.style.cursor = "";
                    }}
                  />
                );
              }
            })}
          </React.Fragment>
        );
      })}
    </Layer>
  );
}
