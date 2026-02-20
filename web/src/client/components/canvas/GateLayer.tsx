import React, { useEffect, useState, useCallback, useMemo } from "react";
import { Layer, Group, Line, Circle, Rect, Text } from "react-konva";
import * as Y from "yjs";
import type Konva from "konva";
import {
  getGatesMap,
  getWiresMap,
  getConnectionsMap,
  addWireModelToDoc,
  addConnectionToDoc,
  readWireModel,
  updateWireModel,
  syncConnectionsFromWire,
} from "../../lib/collab/yjs-schema";
import {
  calcShape,
  updateConnectionPos,
} from "../../lib/canvas/wire-model";
import { useCanvasStore } from "../../stores/canvas-store";
import { useWireState, useWireStates } from "../../stores/simulation-store";
import { GRID_SIZE, SNAP_SIZE, WIRE_STATE } from "@shared/constants";
import type { GateDefinition } from "@shared/types";
import type { WireState } from "@shared/constants";
import { useCanvasColors } from "../../hooks/useCanvasColors";
import type { CanvasColors } from "@shared/theme-colors";

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
  onColor: string, offColor: string,
): React.ReactNode[] {
  const numDigits = hexStr.length;
  const pad = boxW * 0.15;
  const gap = 2;
  const totalGap = gap * (numDigits - 1);
  const digitW = (boxW - pad * 2 - totalGap) / numDigits;
  const digitH = boxH - pad * 2;
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

export { gateDefs as loadedGateDefs, loadGateDefs };

interface GateLayerProps {
  doc: Y.Doc;
  readOnly: boolean;
  onGateDblClick?: (gateId: string) => void;
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

export function getGateBounds(def: GateDefinition) {
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
  // Gates with no shape or pins (e.g. AA_LABEL) get a minimal bounding box
  if (!isFinite(minX)) {
    return { x: 0, y: 0, width: 0, height: 0 };
  }
  return {
    x: minX * GRID_SIZE,
    y: minY * GRID_SIZE,
    width: (maxX - minX) * GRID_SIZE,
    height: (maxY - minY) * GRID_SIZE,
  };
}

const PIN_HIT_RADIUS = 8;

/** Rotate a local-space offset (lx, ly) by `degrees` around the origin, then translate by (gateX, gateY). */
function rotatePin(lx: number, ly: number, gateX: number, gateY: number, degrees: number): { x: number; y: number } {
  if (degrees === 0) return { x: gateX + lx, y: gateY + ly };
  const rad = (degrees * Math.PI) / 180;
  const cos = Math.cos(rad);
  const sin = Math.sin(rad);
  return {
    x: gateX + lx * cos - ly * sin,
    y: gateY + lx * sin + ly * cos,
  };
}

// ---------------------------------------------------------------------------
// LED wire-state wrapper — subscribes to a single wire
// ---------------------------------------------------------------------------

function LedIndicator({ wireId, def }: { wireId: string | undefined; def: GateDefinition }) {
  const state = useWireState(wireId ?? "");
  const colors = useCanvasColors();
  const ledColor = colors.wire[wireId ? state : WIRE_STATE.UNKNOWN];

  const boxStr = def.guiParams?.LED_BOX;
  if (!boxStr) return null;
  const [bx1, rawY1, bx2, rawY2] = boxStr.split(",").map(Number);
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
}

// ---------------------------------------------------------------------------
// REGISTER wire-state wrapper — subscribes to multiple wires
// ---------------------------------------------------------------------------

function RegisterDisplay({
  gate,
  def,
  pinWireMap,
}: {
  gate: GateRenderData;
  def: GateDefinition;
  pinWireMap: Map<string, string>;
}) {
  const colors = useCanvasColors();
  const boxStr = def.guiParams?.VALUE_BOX;
  if (!boxStr) return null;

  const numBits = parseInt(def.params?.INPUT_BITS ?? "0", 10);

  // Collect wireIds for all input bits
  const wireIds = useMemo(() => {
    const ids: string[] = [];
    for (let i = 0; i < numBits; i++) {
      const wid = pinWireMap.get(`${gate.id}:IN_${i}`);
      if (wid) ids.push(wid);
    }
    return ids;
  }, [gate.id, numBits, pinWireMap]);

  const getState = useWireStates(wireIds);

  const [bx1, rawY1, bx2, rawY2] = boxStr.split(",").map(Number);
  const by1 = Math.min(-rawY1, -rawY2);
  const by2 = Math.max(-rawY1, -rawY2);

  // Compute value from input wire states
  let value = 0;
  for (let i = 0; i < numBits; i++) {
    const wireId = pinWireMap.get(`${gate.id}:IN_${i}`);
    if (wireId) {
      const ws = getState(wireId);
      if (ws === WIRE_STATE.ONE) {
        value |= (1 << i);
      }
    }
  }

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
        fill={colors.sevenSegBg}
        listening={false}
      />
      {renderSevenSegDigits(hexStr, boxX, boxY, boxW, boxH, colors.sevenSegOn, colors.sevenSegOff)}
    </>
  );
}

// ---------------------------------------------------------------------------
// Memoized per-gate component
// ---------------------------------------------------------------------------

interface GateShapeProps {
  gate: GateRenderData;
  def: GateDefinition;
  readOnly: boolean;
  connectedWireId: string | undefined; // first connected wireId for LED/NODE
  pinWireMap: Map<string, string>;
  onDragMove: (id: string, e: Konva.KonvaEventObject<DragEvent>) => void;
  onDragEnd: (id: string, e: Konva.KonvaEventObject<DragEvent>) => void;
  onMouseDown: (id: string, e: Konva.KonvaEventObject<MouseEvent>) => void;
  onToggleClick: (id: string, e: Konva.KonvaEventObject<MouseEvent>) => void;
  onDblClick?: (gateId: string) => void;
  onPinMouseDown: (
    gateId: string, gateX: number, gateY: number, gateRotation: number,
    pinName: string, pinDirection: "input" | "output", pinX: number, pinY: number,
    e: Konva.KonvaEventObject<MouseEvent>
  ) => void;
  onPinMouseUp: (
    gateId: string, gateX: number, gateY: number, gateRotation: number,
    pinName: string, pinDirection: "input" | "output", pinX: number, pinY: number,
    e: Konva.KonvaEventObject<MouseEvent>
  ) => void;
  onPinMouseEnter: (
    gateId: string, gateX: number, gateY: number, gateRotation: number,
    pinName: string, pinDirection: "input" | "output", pinX: number, pinY: number,
    e: Konva.KonvaEventObject<MouseEvent>
  ) => void;
  onPinMouseLeave: (e: Konva.KonvaEventObject<MouseEvent>) => void;
}

const GateShape = React.memo(function GateShape({
  gate,
  def,
  readOnly,
  connectedWireId,
  pinWireMap,
  onDragMove,
  onDragEnd,
  onMouseDown,
  onToggleClick,
  onDblClick,
  onPinMouseDown,
  onPinMouseUp,
  onPinMouseEnter,
  onPinMouseLeave,
}: GateShapeProps) {
  const selected = useCanvasStore((s) => !!s.selectedIds[gate.id]);
  const hoveredPin = useCanvasStore((s) => s.hoveredPin);
  const colors = useCanvasColors();

  const strokeColor = selected ? colors.gateSelected : colors.gateStroke;
  const bounds = getGateBounds(def);

  const isToggle = def.guiType === "TOGGLE";
  const toggleOn = isToggle && gate.params.OUTPUT_NUM === "1";
  const isLed = def.guiType === "LED";

  return (
    <Group
      x={gate.x}
      y={gate.y}
      rotation={gate.rotation}
      draggable={!readOnly && !useCanvasStore.getState().wireDrawing}
      onDragMove={(e) => onDragMove(gate.id, e)}
      onDragEnd={(e) => onDragEnd(gate.id, e)}
      onMouseDown={(e) => onMouseDown(gate.id, e)}
      onDblClick={() => onDblClick?.(gate.id)}
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
          fill={toggleOn ? colors.toggleOn : colors.toggleOff}
          cornerRadius={2}
          onMouseDown={(e) => onToggleClick(gate.id, e)}
        />
      )}
      {/* LED state indicator */}
      {isLed && <LedIndicator wireId={connectedWireId} def={def} />}
      {/* REGISTER display */}
      {def.guiType === "REGISTER" && (
        <RegisterDisplay gate={gate} def={def} pinWireMap={pinWireMap} />
      )}
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
      {/* LABEL text */}
      {def.guiType === "LABEL" && (() => {
        const labelText = gate.params.LABEL_TEXT ?? "Text";
        const textHeight = parseFloat(gate.params.TEXT_HEIGHT ?? "2.0");
        const fontSize = textHeight * GRID_SIZE;
        return (
          <Text
            text={labelText}
            fontSize={fontSize}
            fill={selected ? colors.gateSelected : colors.labelText}
            fontFamily="monospace"
            listening={false}
            offsetY={fontSize / 2}
          />
        );
      })()}
      {/* FROM text */}
      {def.guiType === "FROM" && (() => {
        const labelText = gate.params.JUNCTION_ID ?? "";
        const fontSize = 1.5 * GRID_SIZE;
        return (
          <Text
            text={labelText}
            fontSize={fontSize}
            fill={selected ? colors.gateSelected : colors.labelText}
            fontFamily="monospace"
            listening={false}
            x={0}
            y={-fontSize / 2}
            align="right"
            offsetX={fontSize * labelText.length * 0.6}
          />
        );
      })()}
      {/* TO text */}
      {def.guiType === "TO" && (() => {
        const labelText = gate.params.JUNCTION_ID ?? "";
        const fontSize = 1.5 * GRID_SIZE;
        return (
          <Text
            text={labelText}
            fontSize={fontSize}
            fill={selected ? colors.gateSelected : colors.labelText}
            fontFamily="monospace"
            listening={false}
            x={0.4 * GRID_SIZE}
            y={-fontSize / 2}
          />
        );
      })()}
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
                fill={colors.pinHover}
                listening={false}
              />
            )}
            <Circle
              x={pin.x * GRID_SIZE}
              y={pin.y * GRID_SIZE}
              radius={3}
              fill={colors.pinDot}
              listening={false}
            />
            <Circle
              x={pin.x * GRID_SIZE}
              y={pin.y * GRID_SIZE}
              radius={PIN_HIT_RADIUS}
              fill="rgba(0,0,0,0.01)"
              onMouseDown={(e) =>
                onPinMouseDown(gate.id, gate.x, gate.y, gate.rotation, pin.name, "input", pin.x, pin.y, e)
              }
              onMouseUp={(e) =>
                onPinMouseUp(gate.id, gate.x, gate.y, gate.rotation, pin.name, "input", pin.x, pin.y, e)
              }
              onMouseEnter={(e) =>
                onPinMouseEnter(gate.id, gate.x, gate.y, gate.rotation, pin.name, "input", pin.x, pin.y, e)
              }
              onMouseLeave={onPinMouseLeave}
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
                fill={colors.pinHover}
                listening={false}
              />
            )}
            <Circle
              x={pin.x * GRID_SIZE}
              y={pin.y * GRID_SIZE}
              radius={3}
              fill={colors.pinDot}
              listening={false}
            />
            <Circle
              x={pin.x * GRID_SIZE}
              y={pin.y * GRID_SIZE}
              radius={PIN_HIT_RADIUS}
              fill="rgba(0,0,0,0.01)"
              onMouseDown={(e) =>
                onPinMouseDown(gate.id, gate.x, gate.y, gate.rotation, pin.name, "output", pin.x, pin.y, e)
              }
              onMouseUp={(e) =>
                onPinMouseUp(gate.id, gate.x, gate.y, gate.rotation, pin.name, "output", pin.x, pin.y, e)
              }
              onMouseEnter={(e) =>
                onPinMouseEnter(gate.id, gate.x, gate.y, gate.rotation, pin.name, "output", pin.x, pin.y, e)
              }
              onMouseLeave={onPinMouseLeave}
            />
          </React.Fragment>
        );
      })}
    </Group>
  );
});

// ---------------------------------------------------------------------------
// GateLayer — no longer subscribes to wireStates
// ---------------------------------------------------------------------------

export function GateLayer({ doc, readOnly, onGateDblClick }: GateLayerProps) {
  const [gates, setGates] = useState<Map<string, GateRenderData>>(new Map());
  const [defs, setDefs] = useState<GateDefinition[]>([]);
  // Map from gateId to first connected wireId (for LED/NODE state)
  const [gateFirstWire, setGateFirstWire] = useState<Map<string, string>>(new Map());
  // Map from "gateId:pinName" to wireId (for computing register values)
  const [pinWireMap, setPinWireMap] = useState<Map<string, string>>(new Map());
  const selectOnly = useCanvasStore((s) => s.selectOnly);
  const toggleSelection = useCanvasStore((s) => s.toggleSelection);
  const setWireDrawing = useCanvasStore((s) => s.setWireDrawing);
  const setHoveredPin = useCanvasStore((s) => s.setHoveredPin);

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
      const nextFirst = new Map<string, string>();
      const nextPinWire = new Map<string, string>();
      connectionsMap.forEach((yConn) => {
        const gateId = yConn.get("gateId") as string;
        const wireId = yConn.get("wireId") as string;
        const pinName = yConn.get("pinName") as string;
        if (!nextFirst.has(gateId)) nextFirst.set(gateId, wireId);
        nextPinWire.set(`${gateId}:${pinName}`, wireId);
      });
      setGateFirstWire(nextFirst);
      setPinWireMap(nextPinWire);
    }

    syncConns();
    connectionsMap.observeDeep(syncConns);
    return () => connectionsMap.unobserveDeep(syncConns);
  }, [doc]);

  const defsMap = useMemo(() => new Map(defs.map((d) => [d.id, d])), [defs]);

  // Recompute wires connected to a moved gate using updateConnectionPos
  const recomputeConnectedWires = useCallback(
    (id: string, gateX: number, gateY: number) => {
      const gatesMap = getGatesMap(doc);
      const yGate = gatesMap.get(id);
      if (!yGate) return;

      const connectionsMap = getConnectionsMap(doc);
      const wires = getWiresMap(doc);

      // Build a getPinPos that uses the overridden position for the moved gate
      const makePinPos = (gId: string, pName: string) => {
        const g = gatesMap.get(gId);
        if (!g) return { x: 0, y: 0 };
        const def = defsMap.get(g.get("defId"));
        if (!def) return { x: 0, y: 0 };
        const gx = gId === id ? gateX : (g.get("x") as number);
        const gy = gId === id ? gateY : (g.get("y") as number);
        const rot = g.get("rotation") as number ?? 0;
        const allPins = [...def.inputs, ...def.outputs];
        const pin = allPins.find((p) => p.name === pName);
        if (!pin) return { x: gx, y: gy };
        return rotatePin(pin.x * GRID_SIZE, pin.y * GRID_SIZE, gx, gy, rot);
      };

      const makeIsVerticalPin = (gId: string, pName: string) => {
        const g = gatesMap.get(gId);
        if (!g) return false;
        const def = defsMap.get(g.get("defId"));
        if (!def) return false;
        const allPins = [...def.inputs, ...def.outputs];
        const pin = allPins.find((p) => p.name === pName);
        if (!pin) return false;
        return pin.x === 0;
      };

      // Track which wires we've already updated (avoid updating same wire twice)
      const updatedWires = new Set<string>();

      connectionsMap.forEach((yConn) => {
        if (yConn.get("gateId") !== id) return;
        const wireId = yConn.get("wireId") as string;
        if (updatedWires.has(wireId)) return;
        updatedWires.add(wireId);

        const pinName = yConn.get("pinName") as string;
        const yWire = wires.get(wireId);
        if (!yWire) return;

        const model = readWireModel(yWire);
        if (!model) return;

        const updated = updateConnectionPos(
          model,
          id,
          pinName,
          makePinPos,
          makeIsVerticalPin,
        );
        updateWireModel(doc, wireId, updated);
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

      if (def.guiType === "PULSE") {
        yGate.set("param:OUTPUT_NUM", "1");
        setTimeout(() => {
          const stillExists = gatesMap.get(id);
          if (stillExists) stillExists.set("param:OUTPUT_NUM", "0");
        }, 100);
      }
    },
    [selectOnly, toggleSelection, readOnly, doc, defsMap]
  );

  const handleToggleClick = useCallback(
    (id: string, e: Konva.KonvaEventObject<MouseEvent>) => {
      e.cancelBubble = true;
      if (readOnly || e.evt.button !== 0) return;
      const gatesMap = getGatesMap(doc);
      const yGate = gatesMap.get(id);
      if (!yGate) return;
      const current = yGate.get("param:OUTPUT_NUM") ?? "0";
      yGate.set("param:OUTPUT_NUM", current === "1" ? "0" : "1");
    },
    [readOnly, doc]
  );

  const handlePinMouseDown = useCallback(
    (
      gateId: string,
      gateX: number,
      gateY: number,
      gateRotation: number,
      pinName: string,
      pinDirection: "input" | "output",
      pinX: number,
      pinY: number,
      e: Konva.KonvaEventObject<MouseEvent>
    ) => {
      if (readOnly) return;
      e.cancelBubble = true;
      if (e.evt.button !== 0) return;

      const { x: absX, y: absY } = rotatePin(pinX * GRID_SIZE, pinY * GRID_SIZE, gateX, gateY, gateRotation);

      // Always start a new wire drawing (completion happens on mouseUp)
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
    [readOnly, setWireDrawing]
  );

  const handlePinMouseUp = useCallback(
    (
      gateId: string,
      gateX: number,
      gateY: number,
      gateRotation: number,
      pinName: string,
      pinDirection: "input" | "output",
      pinX: number,
      pinY: number,
      e: Konva.KonvaEventObject<MouseEvent>
    ) => {
      if (readOnly) return;
      e.cancelBubble = true;

      const { x: absX, y: absY } = rotatePin(pinX * GRID_SIZE, pinY * GRID_SIZE, gateX, gateY, gateRotation);

      const wd = useCanvasStore.getState().wireDrawing;
      if (!wd) return;

      // Complete the wire — connect source pin to this destination pin
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

      // Don't create a duplicate wire between the same two pins
      const connectionsMap = getConnectionsMap(doc);
      let duplicate = false;
      const srcWires = new Set<string>();
      const dstWires = new Set<string>();
      connectionsMap.forEach((yConn) => {
        const gId = yConn.get("gateId") as string;
        const pName = yConn.get("pinName") as string;
        const wId = yConn.get("wireId") as string;
        if (gId === srcGateId && pName === srcPinName) srcWires.add(wId);
        if (gId === dstGateId && pName === dstPinName) dstWires.add(wId);
      });
      for (const wId of srcWires) {
        if (dstWires.has(wId)) { duplicate = true; break; }
      }
      if (duplicate) {
        setWireDrawing(null);
        setHoveredPin(null);
        return;
      }

      // Build connections and use calcShape for proper routing
      const wireId = crypto.randomUUID();
      const gatesMap = getGatesMap(doc);

      const makePinPos = (gId: string, pName: string) => {
        const g = gatesMap.get(gId);
        if (!g) return { x: 0, y: 0 };
        const def = defsMap.get(g.get("defId"));
        if (!def) return { x: 0, y: 0 };
        const gx = g.get("x") as number;
        const gy = g.get("y") as number;
        const rot = g.get("rotation") as number ?? 0;
        const allPins = [...def.inputs, ...def.outputs];
        const pin = allPins.find((p) => p.name === pName);
        if (!pin) return { x: gx, y: gy };
        return rotatePin(pin.x * GRID_SIZE, pin.y * GRID_SIZE, gx, gy, rot);
      };

      const makeIsVerticalPin = (gId: string, pName: string) => {
        const g = gatesMap.get(gId);
        if (!g) return false;
        const def = defsMap.get(g.get("defId"));
        if (!def) return false;
        const allPins = [...def.inputs, ...def.outputs];
        const pin = allPins.find((p) => p.name === pName);
        if (!pin) return false;
        return pin.x === 0;
      };

      const connections = [
        { gateId: srcGateId, pinName: srcPinName, pinDirection: srcPinDir as "input" | "output" },
        { gateId: dstGateId, pinName: dstPinName, pinDirection: dstPinDir as "input" | "output" },
      ];

      const wireModel = calcShape(connections, makePinPos, makeIsVerticalPin);

      doc.transact(() => {
        addWireModelToDoc(doc, wireId, wireModel);
        syncConnectionsFromWire(doc, wireId, wireModel);
      });

      setWireDrawing(null);
      setHoveredPin(null);
    },
    [readOnly, setWireDrawing, setHoveredPin, doc, defsMap]
  );

  const handlePinMouseEnter = useCallback(
    (
      gateId: string,
      gateX: number,
      gateY: number,
      gateRotation: number,
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

      const { x, y } = rotatePin(pinX * GRID_SIZE, pinY * GRID_SIZE, gateX, gateY, gateRotation);
      setHoveredPin({
        gateId,
        pinName,
        pinDirection,
        x,
        y,
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

        return (
          <GateShape
            key={gate.id}
            gate={gate}
            def={def}
            readOnly={readOnly}
            connectedWireId={gateFirstWire.get(gate.id)}
            pinWireMap={pinWireMap}
            onDragMove={handleDragMove}
            onDragEnd={handleDragEnd}
            onMouseDown={handleMouseDown}
            onToggleClick={handleToggleClick}
            onDblClick={onGateDblClick}
            onPinMouseDown={handlePinMouseDown}
            onPinMouseUp={handlePinMouseUp}
            onPinMouseEnter={handlePinMouseEnter}
            onPinMouseLeave={handlePinMouseLeave}
          />
        );
      })}
    </Layer>
  );
}
