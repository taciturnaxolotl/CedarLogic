import { useEffect, useState, useCallback } from "react";
import { Layer, Group, Line, Circle, Rect } from "react-konva";
import * as Y from "yjs";
import type Konva from "konva";
import { getGatesMap } from "../../lib/collab/yjs-schema";
import { useCanvasStore } from "../../stores/canvas-store";
import { GRID_SIZE, SNAP_SIZE } from "@shared/constants";
import type { GateDefinition } from "@shared/types";

let gateDefs: GateDefinition[] = [];

async function loadGateDefs(): Promise<GateDefinition[]> {
  if (gateDefs.length > 0) return gateDefs;
  try {
    const mod = await import("../../lib/canvas/gate-defs.json");
    gateDefs = mod.default as unknown as GateDefinition[];
  } catch {
    console.warn("Gate definitions not found. Run: bun run parse-gates");
  }
  return gateDefs;
}

interface GateLayerProps {
  doc: Y.Doc;
  readOnly: boolean;
}

interface GateRenderData {
  id: string;
  defId: string;
  x: number;
  y: number;
  rotation: number;
}

function snapToGrid(val: number): number {
  return Math.round(val / SNAP_SIZE) * SNAP_SIZE;
}

function getGateBounds(def: GateDefinition) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const seg of def.shape) {
    minX = Math.min(minX, seg.x1, seg.x2);
    minY = Math.min(minY, seg.y1, seg.y2);
    maxX = Math.max(maxX, seg.x1, seg.x2);
    maxY = Math.max(maxY, seg.y1, seg.y2);
  }
  for (const pin of [...def.inputs, ...def.outputs]) {
    minX = Math.min(minX, pin.x);
    minY = Math.min(minY, pin.y);
    maxX = Math.max(maxX, pin.x);
    maxY = Math.max(maxY, pin.y);
  }
  return {
    x: minX * GRID_SIZE,
    y: minY * GRID_SIZE,
    width: (maxX - minX) * GRID_SIZE,
    height: (maxY - minY) * GRID_SIZE,
  };
}

export function GateLayer({ doc, readOnly }: GateLayerProps) {
  const [gates, setGates] = useState<Map<string, GateRenderData>>(new Map());
  const [defs, setDefs] = useState<GateDefinition[]>([]);
  const selectedIds = useCanvasStore((s) => s.selectedIds);
  const selectOnly = useCanvasStore((s) => s.selectOnly);
  const toggleSelection = useCanvasStore((s) => s.toggleSelection);

  useEffect(() => {
    loadGateDefs().then(setDefs);
  }, []);

  useEffect(() => {
    const gatesMap = getGatesMap(doc);

    function sync() {
      const next = new Map<string, GateRenderData>();
      gatesMap.forEach((yGate, id) => {
        next.set(id, {
          id,
          defId: yGate.get("defId"),
          x: yGate.get("x"),
          y: yGate.get("y"),
          rotation: yGate.get("rotation") ?? 0,
        });
      });
      setGates(next);
    }

    sync();
    gatesMap.observeDeep(sync);
    return () => gatesMap.unobserveDeep(sync);
  }, [doc]);

  const handleDragEnd = useCallback(
    (id: string, e: Konva.KonvaEventObject<DragEvent>) => {
      if (readOnly) return;
      const gatesMap = getGatesMap(doc);
      const yGate = gatesMap.get(id);
      if (!yGate) return;
      doc.transact(() => {
        yGate.set("x", snapToGrid(e.target.x()));
        yGate.set("y", snapToGrid(e.target.y()));
      });
    },
    [doc, readOnly]
  );

  const handleMouseDown = useCallback(
    (id: string, e: Konva.KonvaEventObject<MouseEvent>) => {
      e.cancelBubble = true;
      if (e.evt.button !== 0) return;

      if (e.evt.shiftKey || e.evt.ctrlKey || e.evt.metaKey) {
        toggleSelection(id);
      } else {
        selectOnly(id);
      }
    },
    [selectOnly, toggleSelection]
  );

  const defsMap = new Map(defs.map((d) => [d.id, d]));

  return (
    <Layer>
      {Array.from(gates.values()).map((gate) => {
        const def = defsMap.get(gate.defId);
        if (!def) return null;
        const selected = !!selectedIds[gate.id];
        const strokeColor = selected ? "#3b82f6" : "#e2e8f0";
        const bounds = getGateBounds(def);

        return (
          <Group
            key={gate.id}
            x={gate.x}
            y={gate.y}
            rotation={gate.rotation}
            draggable={!readOnly}
            onDragEnd={(e) => handleDragEnd(gate.id, e)}
            onMouseDown={(e) => handleMouseDown(gate.id, e)}
          >
            {/* Invisible hit area so clicking inside the gate shape works */}
            <Rect
              x={bounds.x}
              y={bounds.y}
              width={bounds.width}
              height={bounds.height}
              fill="transparent"
            />
            {def.shape.map((seg, i) => (
              <Line
                key={i}
                points={[
                  seg.x1 * GRID_SIZE,
                  seg.y1 * GRID_SIZE,
                  seg.x2 * GRID_SIZE,
                  seg.y2 * GRID_SIZE,
                ]}
                stroke={strokeColor}
                strokeWidth={1.5}
                lineCap="round"
                lineJoin="round"
              />
            ))}
            {def.inputs.map((pin) => (
              <Circle
                key={`in-${pin.name}`}
                x={pin.x * GRID_SIZE}
                y={pin.y * GRID_SIZE}
                radius={3}
                fill="#60a5fa"
                name={`pin-input-${pin.name}`}
              />
            ))}
            {def.outputs.map((pin) => (
              <Circle
                key={`out-${pin.name}`}
                x={pin.x * GRID_SIZE}
                y={pin.y * GRID_SIZE}
                radius={3}
                fill="#60a5fa"
                name={`pin-output-${pin.name}`}
              />
            ))}
          </Group>
        );
      })}
    </Layer>
  );
}
