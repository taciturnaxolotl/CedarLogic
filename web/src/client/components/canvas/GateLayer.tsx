import React, { useEffect, useState, useCallback, useMemo } from "react";
import { Layer, Group, Line, Circle, Rect } from "react-konva";
import * as Y from "yjs";
import type Konva from "konva";
import {
  getGatesMap,
  getWiresMap,
  getConnectionsMap,
  addWireToDoc,
  addConnectionToDoc,
} from "../../lib/collab/yjs-schema";
import { useCanvasStore } from "../../stores/canvas-store";
import { useSimulationStore } from "../../stores/simulation-store";
import { GRID_SIZE, SNAP_SIZE, WIRE_COLORS, WIRE_STATE } from "@shared/constants";
import type { GateDefinition, WireSegment } from "@shared/types";
import type { WireState } from "@shared/constants";

/** Remove zero-length segments and merge collinear adjacent segments. */
function cleanSegments(segments: WireSegment[]): WireSegment[] {
  let result = segments.filter((s) => !(s.x1 === s.x2 && s.y1 === s.y2));

  let changed = true;
  while (changed) {
    changed = false;
    for (let i = 0; i < result.length - 1; i++) {
      const a = result[i];
      const b = result[i + 1];
      if (a.y1 === a.y2 && b.y1 === b.y2 && a.y2 === b.y1) {
        result[i] = { x1: a.x1, y1: a.y1, x2: b.x2, y2: a.y1 };
        result.splice(i + 1, 1);
        changed = true;
        break;
      }
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

// 7-segment display rendering
// Segments: a=top, b=top-right, c=bot-right, d=bottom, e=bot-left, f=top-left, g=middle
//   _a_
//  |   |
//  f   b
//  |_g_|
//  |   |
//  e   c
//  |_d_|
const SEVEN_SEG_MAP: Record<string, number[]> = {
  "0": [1,1,1,1,1,1,0], "1": [0,1,1,0,0,0,0], "2": [1,1,0,1,1,0,1], "3": [1,1,1,1,0,0,1],
  "4": [0,1,1,0,0,1,1], "5": [1,0,1,1,0,1,1], "6": [1,0,1,1,1,1,1], "7": [1,1,1,0,0,0,0],
  "8": [1,1,1,1,1,1,1], "9": [1,1,1,1,0,1,1], "A": [1,1,1,0,1,1,1], "B": [0,0,1,1,1,1,1],
  "C": [1,0,0,1,1,1,0], "D": [0,1,1,1,1,0,1], "E": [1,0,0,1,1,1,1], "F": [1,0,0,0,1,1,1],
};

function renderSevenSegDigits(
  hexStr: string, boxX: number, boxY: number, boxW: number, boxH: number,
): React.ReactNode[] {
  const numDigits = hexStr.length;
  const pad = boxW * 0.15;
  const gap = 2;
  const totalGap = gap * (numDigits - 1);
  const digitW = (boxW - pad * 2 - totalGap) / numDigits;
  const digitH = boxH - pad * 2;
  const onColor = "#ff2222";
  const offColor = "#1a0000";
  const sw = Math.max(1.5, Math.min(digitW * 0.15, digitH * 0.08));

  const elements: React.ReactNode[] = [];
  for (let d = 0; d < numDigits; d++) {
    const ch = hexStr[d];
    const segs = SEVEN_SEG_MAP[ch] ?? [0,0,0,0,0,0,0];
    const dx = boxX + pad + d * (digitW + gap);
    const dy = boxY + pad;
    const m = sw; // margin from edge
    const hw = digitW; // full digit width
    const hh = digitH / 2; // half digit height

    // Segment coordinates as [x1, y1, x2, y2]
    const segLines: [number, number, number, number][] = [
      [dx + m, dy,         dx + hw - m, dy],          // a: top
      [dx + hw, dy + m,    dx + hw, dy + hh - m],     // b: top-right
      [dx + hw, dy + hh + m, dx + hw, dy + hh*2 - m], // c: bot-right
      [dx + m, dy + hh*2,  dx + hw - m, dy + hh*2],   // d: bottom
      [dx, dy + hh + m,    dx, dy + hh*2 - m],        // e: bot-left
      [dx, dy + m,         dx, dy + hh - m],          // f: top-left
      [dx + m, dy + hh,    dx + hw - m, dy + hh],     // g: middle
    ];

    for (let s = 0; s < 7; s++) {
      const [x1, y1, x2, y2] = segLines[s];
      elements.push(
        <Line
          key={`seg-${d}-${s}`}
          points={[x1, y1, x2, y2]}
          stroke={segs[s] ? onColor : offColor}
          strokeWidth={sw}
          lineCap="round"
          listening={false}
        />
      );
    }
  }
  return elements;
}

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

export { gateDefs as loadedGateDefs };

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
  params: Record<string, string>;
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

const PIN_HIT_RADIUS = 8;

function getPinPosition(
  gateDef: GateDefinition,
  pinName: string,
  pinDirection: "input" | "output",
  gateX: number,
  gateY: number,
): { x: number; y: number } | null {
  const pins = pinDirection === "input" ? gateDef.inputs : gateDef.outputs;
  const pin = pins.find((p) => p.name === pinName);
  if (!pin) return null;
  return { x: gateX + pin.x * GRID_SIZE, y: gateY + pin.y * GRID_SIZE };
}

export function GateLayer({ doc, readOnly }: GateLayerProps) {
  const [gates, setGates] = useState<Map<string, GateRenderData>>(new Map());
  const [defs, setDefs] = useState<GateDefinition[]>([]);
  // Map from gateId to array of connected wireIds
  const [gateWireMap, setGateWireMap] = useState<Map<string, string[]>>(new Map());
  // Map from "gateId:pinName" to wireId (for computing register values)
  const [pinWireMap, setPinWireMap] = useState<Map<string, string>>(new Map());
  const selectedIds = useCanvasStore((s) => s.selectedIds);
  const selectOnly = useCanvasStore((s) => s.selectOnly);
  const toggleSelection = useCanvasStore((s) => s.toggleSelection);
  const setWireDrawing = useCanvasStore((s) => s.setWireDrawing);
  const hoveredPin = useCanvasStore((s) => s.hoveredPin);
  const setHoveredPin = useCanvasStore((s) => s.setHoveredPin);
  const wireStates = useSimulationStore((s) => s.wireStates);

  useEffect(() => {
    loadGateDefs().then(setDefs);
  }, []);

  useEffect(() => {
    const gatesMap = getGatesMap(doc);

    function sync() {
      const next = new Map<string, GateRenderData>();
      gatesMap.forEach((yGate, id) => {
        const params: Record<string, string> = {};
        for (const [k, v] of yGate.entries()) {
          if (k.startsWith("param:")) params[k.replace("param:", "")] = String(v);
        }
        next.set(id, {
          id,
          defId: yGate.get("defId"),
          x: yGate.get("x"),
          y: yGate.get("y"),
          rotation: yGate.get("rotation") ?? 0,
          params,
        });
      });
      setGates(next);
    }

    sync();
    gatesMap.observeDeep(sync);
    return () => gatesMap.unobserveDeep(sync);
  }, [doc]);

  // Track connections: which wires are connected to each gate
  useEffect(() => {
    const connectionsMap = getConnectionsMap(doc);

    function syncConns() {
      const next = new Map<string, string[]>();
      const nextPinWire = new Map<string, string>();
      connectionsMap.forEach((yConn) => {
        const gateId = yConn.get("gateId") as string;
        const wireId = yConn.get("wireId") as string;
        const pinName = yConn.get("pinName") as string;
        const arr = next.get(gateId) || [];
        arr.push(wireId);
        next.set(gateId, arr);
        nextPinWire.set(`${gateId}:${pinName}`, wireId);
      });
      setGateWireMap(next);
      setPinWireMap(nextPinWire);
    }

    syncConns();
    connectionsMap.observeDeep(syncConns);
    return () => connectionsMap.unobserveDeep(syncConns);
  }, [doc]);

  const defsMap = useMemo(() => new Map(defs.map((d) => [d.id, d])), [defs]);

  // Recompute wire segments connected to a gate at position (gateX, gateY)
  // Uses elastic stretching: only the endpoint segment and its neighbor change;
  // all interior segments (user-placed bends) are preserved.
  const recomputeConnectedWires = useCallback(
    (id: string, gateX: number, gateY: number) => {
      const gatesMap = getGatesMap(doc);
      const yGate = gatesMap.get(id);
      if (!yGate) return;

      const connections = getConnectionsMap(doc);
      const wires = getWiresMap(doc);

      connections.forEach((yConn) => {
        if (yConn.get("gateId") !== id) return;
        const wireId = yConn.get("wireId") as string;

        let otherGateId: string | null = null;
        let otherPinName: string | null = null;
        let otherPinDir: "input" | "output" | null = null;
        connections.forEach((yConn2) => {
          if (yConn2.get("wireId") === wireId && yConn2.get("gateId") !== id) {
            otherGateId = yConn2.get("gateId");
            otherPinName = yConn2.get("pinName");
            otherPinDir = yConn2.get("pinDirection");
          }
        });
        if (!otherGateId || !otherPinName || !otherPinDir) return;

        const otherYGate = gatesMap.get(otherGateId);
        if (!otherYGate) return;

        const movedDef = defsMap.get(yGate.get("defId"));
        const otherDef = defsMap.get(otherYGate.get("defId"));
        if (!movedDef || !otherDef) return;

        const movedPinPos = getPinPosition(
          movedDef, yConn.get("pinName"), yConn.get("pinDirection"),
          gateX, gateY,
        );
        const otherPinPos = getPinPosition(
          otherDef, otherPinName, otherPinDir,
          otherYGate.get("x"), otherYGate.get("y"),
        );
        if (!movedPinPos || !otherPinPos) return;

        const isMovedOutput = yConn.get("pinDirection") === "output";
        const yWire = wires.get(wireId);
        if (!yWire) return;

        let segments: { x1: number; y1: number; x2: number; y2: number }[];
        try {
          segments = JSON.parse(yWire.get("segments") || "[]");
        } catch {
          segments = [];
        }

        // Degenerate: 1 segment — expand to 3-segment Manhattan first
        if (segments.length === 1) {
          const s = segments[0];
          const midX = snapToGrid((s.x1 + s.x2) / 2);
          segments = [
            { x1: s.x1, y1: s.y1, x2: midX, y2: s.y1 },
            { x1: midX, y1: s.y1, x2: midX, y2: s.y2 },
            { x1: midX, y1: s.y2, x2: s.x2, y2: s.y2 },
          ];
        }

        if (segments.length < 2) {
          // Can't do elastic stretch with 0 segments
          return;
        }

        // Determine which end of the wire belongs to the moved gate
        // Output pin is at the start (seg[0].x1,y1), input pin at the end
        if (isMovedOutput) {
          // Moved gate owns the start of the wire
          const newPos = movedPinPos;
          segments[0].x1 = newPos.x;
          segments[0].y1 = newPos.y;
          // Stretch the adjacent segment to maintain connection
          if (segments.length >= 2) {
            // First segment is horizontal: update y of neighbor's start
            segments[0].y2 = newPos.y;
            segments[1].y1 = newPos.y;
          }
        } else {
          // Moved gate owns the end of the wire
          const newPos = movedPinPos;
          const last = segments.length - 1;
          segments[last].x2 = newPos.x;
          segments[last].y2 = newPos.y;
          // Stretch the adjacent segment to maintain connection
          if (segments.length >= 2) {
            // Last segment is horizontal: update y of neighbor's end
            segments[last].y1 = newPos.y;
            segments[last - 1].y2 = newPos.y;
          }
        }

        yWire.set("segments", JSON.stringify(cleanSegments(segments)));
      });
    },
    [doc, defsMap],
  );

  const handleDragMove = useCallback(
    (id: string, e: Konva.KonvaEventObject<DragEvent>) => {
      if (readOnly) return;
      const x = e.target.x();
      const y = e.target.y();
      recomputeConnectedWires(id, x, y);
    },
    [readOnly, recomputeConnectedWires],
  );

  const handleDragEnd = useCallback(
    (id: string, e: Konva.KonvaEventObject<DragEvent>) => {
      if (readOnly) return;
      const gatesMap = getGatesMap(doc);
      const yGate = gatesMap.get(id);
      if (!yGate) return;

      const newX = snapToGrid(e.target.x());
      const newY = snapToGrid(e.target.y());

      // Snap the Konva node position
      e.target.x(newX);
      e.target.y(newY);

      doc.transact(() => {
        yGate.set("x", newX);
        yGate.set("y", newY);
        recomputeConnectedWires(id, newX, newY);
      });
    },
    [doc, readOnly, recomputeConnectedWires],
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

      // Handle interactive gate clicks (TOGGLE, PULSE, KEYPAD)
      if (readOnly) return;
      const gatesMap = getGatesMap(doc);
      const yGate = gatesMap.get(id);
      if (!yGate) return;

      const defId = yGate.get("defId") as string;
      const def = defsMap.get(defId);
      if (!def) return;

      if (def.guiType === "TOGGLE") {
        const current = yGate.get("param:OUTPUT_NUM") ?? "0";
        const next = current === "1" ? "0" : "1";
        yGate.set("param:OUTPUT_NUM", next);
      } else if (def.guiType === "PULSE") {
        yGate.set("param:OUTPUT_NUM", "1");
        setTimeout(() => {
          const stillExists = gatesMap.get(id);
          if (stillExists) stillExists.set("param:OUTPUT_NUM", "0");
        }, 100);
      }
    },
    [selectOnly, toggleSelection, readOnly, doc, defsMap]
  );

  const handlePinMouseDown = useCallback(
    (
      gateId: string,
      gateX: number,
      gateY: number,
      pinName: string,
      pinDirection: "input" | "output",
      pinX: number,
      pinY: number,
      e: Konva.KonvaEventObject<MouseEvent>
    ) => {
      if (readOnly) return;
      e.cancelBubble = true;
      if (e.evt.button !== 0) return;

      const absX = gateX + pinX * GRID_SIZE;
      const absY = gateY + pinY * GRID_SIZE;

      const wd = useCanvasStore.getState().wireDrawing;
      if (wd) {
        // Complete the wire — connect source pin to this destination pin
        // Ensure we connect output→input (swap if needed)
        let srcGateId = wd.fromGateId;
        let srcPinName = wd.fromPinName;
        let srcPinDir = wd.fromPinDirection;
        let srcX = wd.fromX;
        let srcY = wd.fromY;
        let dstGateId = gateId;
        let dstPinName = pinName;
        let dstPinDir = pinDirection;
        let dstX = absX;
        let dstY = absY;

        // Don't connect a pin to itself
        if (srcGateId === dstGateId && srcPinName === dstPinName) {
          setWireDrawing(null);
          setHoveredPin(null);
          return;
        }

        // Don't connect two pins of the same direction
        if (srcPinDir === dstPinDir) {
          setWireDrawing(null);
          setHoveredPin(null);
          return;
        }

        // Normalize so src is output, dst is input
        if (srcPinDir === "input") {
          [srcGateId, srcPinName, srcPinDir, srcX, srcY, dstGateId, dstPinName, dstPinDir, dstX, dstY] =
            [dstGateId, dstPinName, dstPinDir, dstX, dstY, srcGateId, srcPinName, srcPinDir, srcX, srcY];
        }

        // Create wire with 3-segment Manhattan routing
        const wireId = crypto.randomUUID();
        const midX = snapToGrid((srcX + dstX) / 2);
        const segments = [
          { x1: srcX, y1: srcY, x2: midX, y2: srcY },
          { x1: midX, y1: srcY, x2: midX, y2: dstY },
          { x1: midX, y1: dstY, x2: dstX, y2: dstY },
        ];

        doc.transact(() => {
          addWireToDoc(doc, wireId, segments);
          addConnectionToDoc(doc, srcGateId, srcPinName, srcPinDir, wireId);
          addConnectionToDoc(doc, dstGateId, dstPinName, dstPinDir, wireId);
        });

        setWireDrawing(null);
        setHoveredPin(null);
        return;
      }

      // Start a new wire drawing
      setWireDrawing({
        fromGateId: gateId,
        fromPinName: pinName,
        fromPinDirection: pinDirection,
        fromX: absX,
        fromY: absY,
        currentX: absX,
        currentY: absY,
      });
    },
    [readOnly, setWireDrawing, setHoveredPin, doc]
  );

  const handlePinMouseEnter = useCallback(
    (
      gateId: string,
      gateX: number,
      gateY: number,
      pinName: string,
      pinDirection: "input" | "output",
      pinX: number,
      pinY: number,
      e: Konva.KonvaEventObject<MouseEvent>,
    ) => {
      const container = e.target.getStage()?.container();
      if (container) container.style.cursor = "crosshair";

      const wd = useCanvasStore.getState().wireDrawing;
      if (!wd) return;
      // Only highlight if compatible: different direction, different gate
      if (wd.fromPinDirection === pinDirection) return;
      if (wd.fromGateId === gateId) return;

      setHoveredPin({
        gateId,
        pinName,
        pinDirection,
        x: gateX + pinX * GRID_SIZE,
        y: gateY + pinY * GRID_SIZE,
      });
    },
    [setHoveredPin],
  );

  const handlePinMouseLeave = useCallback(
    (e: Konva.KonvaEventObject<MouseEvent>) => {
      const container = e.target.getStage()?.container();
      if (container) container.style.cursor = "";
      setHoveredPin(null);
    },
    [setHoveredPin],
  );

  return (
    <Layer>
      {Array.from(gates.values()).map((gate) => {
        const def = defsMap.get(gate.defId);
        if (!def) return null;
        const selected = !!selectedIds[gate.id];
        const strokeColor = selected ? "#3b82f6" : "#e2e8f0";
        const bounds = getGateBounds(def);

        const isToggle = def.guiType === "TOGGLE";
        const toggleOn = isToggle && gate.params.OUTPUT_NUM === "1";
        const isLed = def.guiType === "LED";

        // Get wire state for LED/NODE visualization
        let gateWireState: WireState = WIRE_STATE.UNKNOWN;
        const connectedWires = gateWireMap.get(gate.id);
        if (connectedWires) {
          for (const wid of connectedWires) {
            const ws = wireStates.get(wid);
            if (ws !== undefined) {
              gateWireState = ws;
              break;
            }
          }
        }
        const ledColor = WIRE_COLORS[gateWireState];

        return (
          <Group
            key={gate.id}
            x={gate.x}
            y={gate.y}
            rotation={gate.rotation}
            draggable={!readOnly && !useCanvasStore.getState().wireDrawing}
            onDragMove={(e) => handleDragMove(gate.id, e)}
            onDragEnd={(e) => handleDragEnd(gate.id, e)}
            onMouseDown={(e) => handleMouseDown(gate.id, e)}
          >
            {/* Invisible hit area */}
            <Rect
              x={bounds.x}
              y={bounds.y}
              width={bounds.width}
              height={bounds.height}
              fill="rgba(0,0,0,0.01)"
            />
            {/* Toggle state indicator */}
            {isToggle && (
              <Rect
                x={-0.6 * GRID_SIZE}
                y={-0.6 * GRID_SIZE}
                width={1.2 * GRID_SIZE}
                height={1.2 * GRID_SIZE}
                fill={toggleOn ? "#ef4444" : "#1e293b"}
                cornerRadius={2}
                listening={false}
              />
            )}
            {/* LED state indicator — fill the LED_BOX area */}
            {isLed && (() => {
              const boxStr = def.guiParams?.LED_BOX;
              if (!boxStr) return null;
              const [bx1, rawY1, bx2, rawY2] = boxStr.split(",").map(Number);
              // Flip Y (guiParams are in Y-up space) and ensure min/max order
              const by1 = Math.min(-rawY1, -rawY2);
              const by2 = Math.max(-rawY1, -rawY2);
              return (
                <Rect
                  x={bx1 * GRID_SIZE}
                  y={by1 * GRID_SIZE}
                  width={(bx2 - bx1) * GRID_SIZE}
                  height={(by2 - by1) * GRID_SIZE}
                  fill={ledColor}
                  opacity={0.85}
                  listening={false}
                />
              );
            })()}
            {/* REGISTER display — 7-segment style */}
            {def.guiType === "REGISTER" && (() => {
              const boxStr = def.guiParams?.VALUE_BOX;
              if (!boxStr) return null;
              const [bx1, rawY1, bx2, rawY2] = boxStr.split(",").map(Number);
              const by1 = Math.min(-rawY1, -rawY2);
              const by2 = Math.max(-rawY1, -rawY2);

              // Compute value from input wire states
              let value = 0;
              const numBits = parseInt(def.params?.INPUT_BITS ?? "0", 10);
              for (let i = 0; i < numBits; i++) {
                const wireId = pinWireMap.get(`${gate.id}:IN_${i}`);
                if (wireId) {
                  const ws = wireStates.get(wireId);
                  if (ws === WIRE_STATE.ONE) {
                    value |= (1 << i);
                  }
                }
              }

              // Convert to hex string, determine how many digits to show
              const hexDigits = Math.max(1, Math.ceil(numBits / 4));
              const hexStr = value.toString(16).toUpperCase().padStart(hexDigits, "0");

              const boxW = (bx2 - bx1) * GRID_SIZE;
              const boxH = (by2 - by1) * GRID_SIZE;
              const boxX = bx1 * GRID_SIZE;
              const boxY = by1 * GRID_SIZE;

              return (
                <>
                  <Rect
                    x={boxX}
                    y={boxY}
                    width={boxW}
                    height={boxH}
                    fill="#0a0a0a"
                    listening={false}
                  />
                  {renderSevenSegDigits(hexStr, boxX, boxY, boxW, boxH)}
                </>
              );
            })()}
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
            {def.circles?.map((c, i) => (
              <Circle
                key={`circle-${i}`}
                x={c.cx * GRID_SIZE}
                y={c.cy * GRID_SIZE}
                radius={c.r * GRID_SIZE}
                stroke={strokeColor}
                strokeWidth={1.5}
                listening={false}
              />
            ))}
            {/* Input pins */}
            {def.inputs.map((pin) => {
              const isHovered = hoveredPin?.gateId === gate.id && hoveredPin?.pinName === pin.name && hoveredPin?.pinDirection === "input";
              return (
                <React.Fragment key={`in-group-${pin.name}`}>
                  {isHovered && (
                    <Rect
                      x={pin.x * GRID_SIZE - 5}
                      y={pin.y * GRID_SIZE - 5}
                      width={10}
                      height={10}
                      fill="#ef4444"
                      listening={false}
                    />
                  )}
                  <Circle
                    x={pin.x * GRID_SIZE}
                    y={pin.y * GRID_SIZE}
                    radius={3}
                    fill="#60a5fa"
                    listening={false}
                  />
                  <Circle
                    x={pin.x * GRID_SIZE}
                    y={pin.y * GRID_SIZE}
                    radius={PIN_HIT_RADIUS}
                    fill="rgba(0,0,0,0.01)"
                    onMouseDown={(e) =>
                      handlePinMouseDown(gate.id, gate.x, gate.y, pin.name, "input", pin.x, pin.y, e)
                    }
                    onMouseEnter={(e) =>
                      handlePinMouseEnter(gate.id, gate.x, gate.y, pin.name, "input", pin.x, pin.y, e)
                    }
                    onMouseLeave={handlePinMouseLeave}
                  />
                </React.Fragment>
              );
            })}
            {/* Output pins */}
            {def.outputs.map((pin) => {
              const isHovered = hoveredPin?.gateId === gate.id && hoveredPin?.pinName === pin.name && hoveredPin?.pinDirection === "output";
              return (
                <React.Fragment key={`out-group-${pin.name}`}>
                  {isHovered && (
                    <Rect
                      x={pin.x * GRID_SIZE - 5}
                      y={pin.y * GRID_SIZE - 5}
                      width={10}
                      height={10}
                      fill="#ef4444"
                      listening={false}
                    />
                  )}
                  <Circle
                    x={pin.x * GRID_SIZE}
                    y={pin.y * GRID_SIZE}
                    radius={3}
                    fill="#60a5fa"
                    listening={false}
                  />
                  <Circle
                    x={pin.x * GRID_SIZE}
                    y={pin.y * GRID_SIZE}
                    radius={PIN_HIT_RADIUS}
                    fill="rgba(0,0,0,0.01)"
                    onMouseDown={(e) =>
                      handlePinMouseDown(gate.id, gate.x, gate.y, pin.name, "output", pin.x, pin.y, e)
                    }
                    onMouseEnter={(e) =>
                      handlePinMouseEnter(gate.id, gate.x, gate.y, pin.name, "output", pin.x, pin.y, e)
                    }
                    onMouseLeave={handlePinMouseLeave}
                  />
                </React.Fragment>
              );
            })}
          </Group>
        );
      })}
    </Layer>
  );
}
