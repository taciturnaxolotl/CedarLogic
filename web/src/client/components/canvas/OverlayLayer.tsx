import { useEffect, useState, useMemo } from "react";
import { Layer, Rect, Line, Group } from "react-konva";
import { useCanvasStore } from "../../stores/canvas-store";
import { GRID_SIZE } from "@shared/constants";
import type { GateDefinition } from "@shared/types";
import { loadedGateDefs, getGateBounds } from "./GateLayer";

export function OverlayLayer() {
  const { selectionBox, wireDrawing, pendingPaste, pendingGate } = useCanvasStore();

  const defsMap = useMemo(
    () => new Map<string, GateDefinition>(loadedGateDefs.map((d) => [d.id, d])),
    // loadedGateDefs is a module-level array that gets populated once
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [loadedGateDefs.length]
  );

  return (
    <Layer>
      {/* Selection box */}
      {selectionBox && (
        <Rect
          x={selectionBox.x}
          y={selectionBox.y}
          width={selectionBox.width}
          height={selectionBox.height}
          fill="rgba(59, 130, 246, 0.1)"
          stroke="#3b82f6"
          strokeWidth={1}
          dash={[4, 4]}
        />
      )}

      {/* Wire drawing preview — straight line from source pin to cursor */}
      {wireDrawing && (
        <Line
          points={[wireDrawing.fromX, wireDrawing.fromY, wireDrawing.currentX, wireDrawing.currentY]}
          stroke="#3b82f6"
          strokeWidth={2}
          lineCap="round"
          lineJoin="round"
          dash={[6, 3]}
          listening={false}
        />
      )}

      {/* Pending paste preview — ghost of gates and wires following the cursor */}
      {pendingPaste && (
        <Group opacity={0.5} listening={false}>
          {/* Ghost gates */}
          {pendingPaste.data.gates.map((gate) => {
            const def = defsMap.get(gate.defId);
            if (!def) return null;
            const gx = pendingPaste.x + gate.offsetX;
            const gy = pendingPaste.y + gate.offsetY;
            return (
              <Group key={gate.originalId} x={gx} y={gy} rotation={gate.rotation}>
                {def.shape.map((seg, i) => (
                  <Line
                    key={i}
                    points={[
                      seg.x1 * GRID_SIZE,
                      seg.y1 * GRID_SIZE,
                      seg.x2 * GRID_SIZE,
                      seg.y2 * GRID_SIZE,
                    ]}
                    stroke="#3b82f6"
                    strokeWidth={1.5}
                    lineCap="round"
                    lineJoin="round"
                  />
                ))}
              </Group>
            );
          })}
          {/* Ghost wires */}
          {pendingPaste.data.wires.map((wire) => {
            // Render from WireModel or legacy segments
            if (wire.model) {
              return Object.values(wire.model.segMap).map((seg) => (
                <Line
                  key={`${wire.originalId}-${seg.id}`}
                  points={[
                    seg.begin.x + pendingPaste.x,
                    seg.begin.y + pendingPaste.y,
                    seg.end.x + pendingPaste.x,
                    seg.end.y + pendingPaste.y,
                  ]}
                  stroke="#3b82f6"
                  strokeWidth={2}
                  lineCap="round"
                  lineJoin="round"
                />
              ));
            }
            return (wire.segments ?? []).map((seg, i) => (
              <Line
                key={`${wire.originalId}-${i}`}
                points={[
                  seg.x1 + pendingPaste.x,
                  seg.y1 + pendingPaste.y,
                  seg.x2 + pendingPaste.x,
                  seg.y2 + pendingPaste.y,
                ]}
                stroke="#3b82f6"
                strokeWidth={2}
                lineCap="round"
                lineJoin="round"
              />
            ));
          })}
        </Group>
      )}

      {/* Pending gate placement ghost */}
      {pendingGate && (() => {
        const def = defsMap.get(pendingGate.defId);
        if (!def) return null;
        return (
          <Group opacity={0.5} listening={false} x={pendingGate.x} y={pendingGate.y} rotation={pendingGate.rotation ?? 0}>
            {def.shape.map((seg, i) => (
              <Line
                key={i}
                points={[
                  seg.x1 * GRID_SIZE,
                  seg.y1 * GRID_SIZE,
                  seg.x2 * GRID_SIZE,
                  seg.y2 * GRID_SIZE,
                ]}
                stroke="#3b82f6"
                strokeWidth={1.5}
                lineCap="round"
                lineJoin="round"
              />
            ))}
          </Group>
        );
      })()}
    </Layer>
  );
}
