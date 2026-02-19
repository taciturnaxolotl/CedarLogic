import { Layer, Line } from "react-konva";
import { GRID_SIZE } from "@shared/constants";

const GRID_EXTENT = 10000;
const GRID_COLOR = "#1e293b";

export function GridLayer() {
  const lines: React.JSX.Element[] = [];

  for (let i = -GRID_EXTENT; i <= GRID_EXTENT; i += GRID_SIZE) {
    lines.push(
      <Line
        key={`h-${i}`}
        points={[-GRID_EXTENT, i, GRID_EXTENT, i]}
        stroke={GRID_COLOR}
        strokeWidth={0.5}
      />,
      <Line
        key={`v-${i}`}
        points={[i, -GRID_EXTENT, i, GRID_EXTENT]}
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
