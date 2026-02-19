import { Layer, Rect, Line } from "react-konva";
import { useCanvasStore } from "../../stores/canvas-store";

export function OverlayLayer() {
  const { selectionBox, wireDrawing } = useCanvasStore();

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
    </Layer>
  );
}
