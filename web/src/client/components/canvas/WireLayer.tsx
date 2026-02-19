import React, { useEffect, useState, useCallback, useRef, useMemo } from "react";
import { Layer, Line, Rect, Circle } from "react-konva";
import * as Y from "yjs";
import type Konva from "konva";
import {
  getWiresMap,
  getGatesMap,
  getConnectionsMap,
  readWireModel,
  updateWireModel,
} from "../../lib/collab/yjs-schema";
import {
  startSegDrag,
  updateSegDrag,
  endSegDrag,
  generateRenderInfo,
  cloneModel,
} from "../../lib/canvas/wire-model";
import type { DragState } from "../../lib/canvas/wire-model";
import { useSimulationStore } from "../../stores/simulation-store";
import { useCanvasStore } from "../../stores/canvas-store";
import { WIRE_COLORS, WIRE_STATE, SNAP_SIZE, GRID_SIZE } from "@shared/constants";
import type { WireModel, WireRenderInfo } from "@shared/wire-types";
import type { GateDefinition } from "@shared/types";

interface WireLayerProps {
  doc: Y.Doc;
  readOnly: boolean;
}

interface WireRenderData {
  id: string;
  model: WireModel;
  renderInfo: WireRenderInfo;
}

function snapToGrid(val: number): number {
  return Math.round(val / SNAP_SIZE) * SNAP_SIZE;
}

/** Build a getPinPos function from current Yjs gate data + gate definitions. */
function makePinPosFn(
  doc: Y.Doc,
  gateDefs: GateDefinition[],
) {
  const defsMap = new Map(gateDefs.map((d) => [d.id, d]));
  const gatesMap = getGatesMap(doc);

  return (gateId: string, pinName: string) => {
    const yGate = gatesMap.get(gateId);
    if (!yGate) return { x: 0, y: 0 };
    const def = defsMap.get(yGate.get("defId"));
    if (!def) return { x: 0, y: 0 };
    const gx = yGate.get("x") as number;
    const gy = yGate.get("y") as number;
    const allPins = [...def.inputs, ...def.outputs];
    const pin = allPins.find((p) => p.name === pinName);
    if (!pin) return { x: gx, y: gy };
    return { x: gx + pin.x * GRID_SIZE, y: gy + pin.y * GRID_SIZE };
  };
}

export function WireLayer({ doc, readOnly }: WireLayerProps) {
  const [wires, setWires] = useState<Map<string, WireRenderData>>(new Map());
  const [gateDefs, setGateDefs] = useState<GateDefinition[]>([]);
  const wireStates = useSimulationStore((s) => s.wireStates);
  const selectedIds = useCanvasStore((s) => s.selectedIds);
  const selectOnly = useCanvasStore((s) => s.selectOnly);

  // Local drag preview — rendered directly from React state, bypasses Yjs round-trip
  const [dragPreview, setDragPreview] = useState<{
    wireId: string;
    model: WireModel;
    renderInfo: WireRenderInfo;
  } | null>(null);

  // Mutable drag tracking (not for rendering — just for computing deltas)
  const dragRef = useRef<{
    wireId: string;
    segId: number;
    dragState: DragState;
    lastWorldPos: number;
    startClientX: number;
    startClientY: number;
    startWorldPos: number;
    isVertical: boolean;
  } | null>(null);

  useEffect(() => {
    import("../../lib/canvas/gate-defs.json").then((mod) => {
      setGateDefs(mod.default as unknown as GateDefinition[]);
    }).catch(() => {});
  }, []);

  const getPinPos = useMemo(
    () => makePinPosFn(doc, gateDefs),
    [doc, gateDefs],
  );

  useEffect(() => {
    const wiresMap = getWiresMap(doc);

    function sync() {
      const next = new Map<string, WireRenderData>();
      wiresMap.forEach((yWire, id) => {
        const model = readWireModel(yWire);
        if (!model) return;
        const renderInfo = generateRenderInfo(model, getPinPos);
        next.set(id, { id, model, renderInfo });
      });
      setWires(next);
    }

    sync();
    wiresMap.observeDeep(sync);
    return () => wiresMap.unobserveDeep(sync);
  }, [doc, getPinPos]);

  // We use refs for the handlers so that addEventListener always gets the latest version
  const getPinPosRef = useRef(getPinPos);
  getPinPosRef.current = getPinPos;

  const handleMouseMove = useCallback(
    (e: MouseEvent) => {
      const drag = dragRef.current;
      if (!drag) return;

      const { zoom } = useCanvasStore.getState();
      const clientDelta = drag.isVertical
        ? e.clientX - drag.startClientX
        : e.clientY - drag.startClientY;
      const newPos = snapToGrid(drag.startWorldPos + clientDelta / zoom);

      const delta = newPos - drag.lastWorldPos;
      if (delta === 0) return;

      const pinPos = getPinPosRef.current;
      const updated = updateSegDrag(drag.dragState, delta, pinPos);
      drag.dragState = updated;
      drag.lastWorldPos = newPos;

      // Update local preview state (synchronous React render — no Yjs round-trip)
      const renderInfo = generateRenderInfo(updated.wire, pinPos);
      setDragPreview({ wireId: drag.wireId, model: updated.wire, renderInfo });
    },
    [], // no deps — reads everything from refs and getState()
  );

  const handleMouseUp = useCallback(
    () => {
      const drag = dragRef.current;
      if (!drag) return;

      const pinPos = getPinPosRef.current;
      const final = endSegDrag(drag.dragState, pinPos);
      updateWireModel(doc, drag.wireId, final);

      dragRef.current = null;
      setDragPreview(null);

      window.removeEventListener("mousemove", handleMouseMove);
      window.removeEventListener("mouseup", handleMouseUp);
    },
    [doc, handleMouseMove],
  );

  const handleSegmentMouseDown = useCallback(
    (wireId: string, segId: number, e: Konva.KonvaEventObject<MouseEvent>) => {
      if (readOnly || e.evt.button !== 0) return;
      e.cancelBubble = true;

      selectOnly(wireId);

      const wire = wires.get(wireId);
      if (!wire) return;
      const pinPos = getPinPosRef.current;
      const ds = startSegDrag(wire.model, segId, pinPos);
      if (ds.dragSegId === -1) return;

      const seg = ds.wire.segMap[ds.dragSegId];
      if (!seg) return;

      const initPos = seg.vertical ? seg.begin.x : seg.begin.y;

      console.log("[WireLayer drag] start", {
        wireId,
        segId,
        isVertical: seg.vertical,
        initPos,
        segBegin: seg.begin,
        segEnd: seg.end,
        clientX: e.evt.clientX,
        clientY: e.evt.clientY,
      });

      dragRef.current = {
        wireId,
        segId,
        dragState: ds,
        lastWorldPos: initPos,
        startClientX: e.evt.clientX,
        startClientY: e.evt.clientY,
        startWorldPos: initPos,
        isVertical: seg.vertical,
      };

      window.addEventListener("mousemove", handleMouseMove);
      window.addEventListener("mouseup", handleMouseUp);
    },
    [readOnly, wires, selectOnly, handleMouseMove, handleMouseUp],
  );

  // Cleanup listeners on unmount
  useEffect(() => {
    return () => {
      window.removeEventListener("mousemove", handleMouseMove);
      window.removeEventListener("mouseup", handleMouseUp);
    };
  }, [handleMouseMove, handleMouseUp]);

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

        // Use drag preview if this wire is being dragged, otherwise use Yjs data
        const preview = dragPreview?.wireId === wire.id ? dragPreview : null;
        const model = preview?.model ?? wire.model;
        const { lineSegments, intersectPoints } = preview?.renderInfo ?? wire.renderInfo;

        return (
          <React.Fragment key={wire.id}>
            {/* Render each segment as an independent line */}
            {lineSegments.map((seg, i) => (
              <Line
                key={`line-${i}`}
                points={[seg.x1, seg.y1, seg.x2, seg.y2]}
                stroke={strokeColor}
                strokeWidth={2}
                lineCap="round"
                lineJoin="round"
                hitStrokeWidth={10}
                onMouseDown={(e) => handleWireClick(wire.id, e)}
              />
            ))}

            {/* T-junction dots */}
            {intersectPoints.map((pt, i) => (
              <Circle
                key={`isect-${i}`}
                x={pt.x}
                y={pt.y}
                radius={3}
                fill={strokeColor}
                listening={false}
              />
            ))}

            {/* Drag handles keyed by segment ID */}
            {!readOnly && Object.values(model.segMap).map((seg) => {
              const horiz = !seg.vertical;

              if (horiz) {
                const minX = Math.min(seg.begin.x, seg.end.x);
                const segWidth = Math.abs(seg.end.x - seg.begin.x);
                return (
                  <Rect
                    key={`drag-${seg.id}`}
                    x={minX}
                    y={seg.begin.y}
                    width={segWidth}
                    height={0}
                    stroke="transparent"
                    strokeWidth={12}
                    hitStrokeWidth={12}
                    onMouseDown={(e) => handleSegmentMouseDown(wire.id, seg.id, e)}
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
                const minY = Math.min(seg.begin.y, seg.end.y);
                const segHeight = Math.abs(seg.end.y - seg.begin.y);
                return (
                  <Rect
                    key={`drag-${seg.id}`}
                    x={seg.begin.x}
                    y={minY}
                    width={0}
                    height={segHeight}
                    stroke="transparent"
                    strokeWidth={12}
                    hitStrokeWidth={12}
                    onMouseDown={(e) => handleSegmentMouseDown(wire.id, seg.id, e)}
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
