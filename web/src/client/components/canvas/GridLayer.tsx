import { Layer, Line } from "react-konva";
import { GRID_SIZE } from "@shared/constants";
import { useCanvasStore } from "../../stores/canvas-store";

const GRID_COLOR = "#1e293b";

export function GridLayer() {
  const viewportX = useCanvasStore((s) => s.viewportX);
  const viewportY = useCanvasStore((s) => s.viewportY);
  const zoom = useCanvasStore((s) => s.zoom);
  const canvasSize = useCanvasStore((s) => s.canvasSize);

  // Compute the visible world-space region
  const left = -viewportX / zoom;
  const top = -viewportY / zoom;
  const right = left + canvasSize.width / zoom;
  const bottom = top + canvasSize.height / zoom;

  // Snap to grid boundaries with a small margin
  const startX = Math.floor(left / GRID_SIZE) * GRID_SIZE - GRID_SIZE;
  const endX = Math.ceil(right / GRID_SIZE) * GRID_SIZE + GRID_SIZE;
  const startY = Math.floor(top / GRID_SIZE) * GRID_SIZE - GRID_SIZE;
  const endY = Math.ceil(bottom / GRID_SIZE) * GRID_SIZE + GRID_SIZE;

  const lines: React.JSX.Element[] = [];

  for (let y = startY; y <= endY; y += GRID_SIZE) {
    lines.push(
      <Line
        key={`h-${y}`}
        points={[startX, y, endX, y]}
        stroke={GRID_COLOR}
        strokeWidth={0.5}
      />
    );
  }
  for (let x = startX; x <= endX; x += GRID_SIZE) {
    lines.push(
      <Line
        key={`v-${x}`}
        points={[x, startY, x, endY]}
        stroke={GRID_COLOR}
        strokeWidth={0.5}
      />
    );
  }

  return (
    <Layer listening={false}>
      {lines}
    </Layer>
  );
}
