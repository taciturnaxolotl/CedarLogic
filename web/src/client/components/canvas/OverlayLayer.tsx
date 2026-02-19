import { Layer, Rect, Circle, Text } from "react-konva";
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

      {/* Wire drawing preview */}
      {wireDrawing &&
        wireDrawing.segments.map((seg, i) => (
          <Rect
            key={`wd-${i}`}
            x={Math.min(seg.x1, seg.x2)}
            y={Math.min(seg.y1, seg.y2)}
            width={Math.abs(seg.x2 - seg.x1) || 2}
            height={Math.abs(seg.y2 - seg.y1) || 2}
            fill="#3b82f6"
            opacity={0.5}
          />
        ))}
    </Layer>
  );
}
