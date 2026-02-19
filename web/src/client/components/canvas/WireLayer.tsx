import React, { useEffect, useState, useCallback } from "react";
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

export function WireLayer({ doc, readOnly }: WireLayerProps) {
  const [wires, setWires] = useState<Map<string, WireRenderData>>(new Map());
  const wireStates = useSimulationStore((s) => s.wireStates);
  const selectedIds = useCanvasStore((s) => s.selectedIds);
  const selectOnly = useCanvasStore((s) => s.selectOnly);

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

  const handleBendDrag = useCallback(
    (wireId: string, e: Konva.KonvaEventObject<DragEvent>) => {
      if (readOnly) return;
      const wiresMap = getWiresMap(doc);
      const yWire = wiresMap.get(wireId);
      if (!yWire) return;

      try {
        const segments: WireSegment[] = JSON.parse(yWire.get("segments") || "[]");
        if (segments.length < 3) return;

        const newMidX = snapToGrid(e.target.x());

        // Update the bend X across all 3 segments
        segments[0].x2 = newMidX;
        segments[1].x1 = newMidX;
        segments[1].x2 = newMidX;
        segments[2].x1 = newMidX;

        yWire.set("segments", JSON.stringify(segments));
      } catch {
        // ignore
      }
    },
    [doc, readOnly],
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

        // Find bend point for draggable handle (3-segment Manhattan wire)
        const hasBend = wire.segments.length >= 3;
        const bendX = hasBend ? wire.segments[1].x1 : 0;
        const bendY1 = hasBend ? wire.segments[1].y1 : 0;
        const bendY2 = hasBend ? wire.segments[1].y2 : 0;

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
            {/* Draggable bend handle on the vertical segment */}
            {hasBend && !readOnly && (
              <Rect
                x={bendX}
                y={Math.min(bendY1, bendY2)}
                width={0}
                height={Math.abs(bendY2 - bendY1)}
                stroke="transparent"
                strokeWidth={12}
                hitStrokeWidth={12}
                draggable
                dragBoundFunc={(pos) => {
                  // Constrain to horizontal movement only
                  const stage = pos as { x: number; y: number };
                  return { x: stage.x, y: Math.min(bendY1, bendY2) };
                }}
                onDragEnd={(e) => handleBendDrag(wire.id, e)}
                onMouseEnter={(e) => {
                  const container = e.target.getStage()?.container();
                  if (container) container.style.cursor = "col-resize";
                }}
                onMouseLeave={(e) => {
                  const container = e.target.getStage()?.container();
                  if (container) container.style.cursor = "";
                }}
              />
            )}
          </React.Fragment>
        );
      })}
    </Layer>
  );
}
