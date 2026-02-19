import Konva from "konva";
import type { GateDefinition } from "@shared/types";
import { GRID_SIZE } from "@shared/constants";

const PIN_RADIUS = 3;
const STROKE_COLOR = "#e2e8f0";
const PIN_COLOR = "#60a5fa";
const SELECTED_COLOR = "#3b82f6";

export function createGateShape(
  def: GateDefinition,
  id: string,
  x: number,
  y: number,
  rotation: number,
  selected: boolean = false
): Konva.Group {
  const group = new Konva.Group({
    id,
    x,
    y,
    rotation,
    draggable: true,
  });

  // Draw shape lines
  for (const seg of def.shape) {
    const line = new Konva.Line({
      points: [
        seg.x1 * GRID_SIZE,
        seg.y1 * GRID_SIZE,
        seg.x2 * GRID_SIZE,
        seg.y2 * GRID_SIZE,
      ],
      stroke: selected ? SELECTED_COLOR : STROKE_COLOR,
      strokeWidth: 1.5,
      lineCap: "round",
      lineJoin: "round",
    });
    group.add(line);
  }

  // Draw input pins
  for (const pin of def.inputs) {
    const circle = new Konva.Circle({
      x: pin.x * GRID_SIZE,
      y: pin.y * GRID_SIZE,
      radius: PIN_RADIUS,
      fill: PIN_COLOR,
      name: `pin-input-${pin.name}`,
    });
    group.add(circle);
  }

  // Draw output pins
  for (const pin of def.outputs) {
    const circle = new Konva.Circle({
      x: pin.x * GRID_SIZE,
      y: pin.y * GRID_SIZE,
      radius: PIN_RADIUS,
      fill: PIN_COLOR,
      name: `pin-output-${pin.name}`,
    });
    group.add(circle);
  }

  return group;
}

export function getGateBounds(def: GateDefinition): {
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
} {
  let minX = Infinity,
    minY = Infinity,
    maxX = -Infinity,
    maxY = -Infinity;

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

  return { minX, minY, maxX, maxY };
}
