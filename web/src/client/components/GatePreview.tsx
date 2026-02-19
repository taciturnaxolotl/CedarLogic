import type { GateDefinition } from "@shared/types";

export function getGatePreviewBounds(def: GateDefinition) {
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
  return { minX, minY, maxX, maxY, width: maxX - minX, height: maxY - minY };
}

export function GatePreview({ def }: { def: GateDefinition }) {
  const bounds = getGatePreviewBounds(def);
  const padding = 0.5;
  const vbX = bounds.minX - padding;
  const vbY = bounds.minY - padding;
  const vbW = bounds.width + padding * 2;
  const vbH = bounds.height + padding * 2;

  return (
    <svg
      viewBox={`${vbX} ${vbY} ${vbW} ${vbH}`}
      className="w-full h-full"
      preserveAspectRatio="xMidYMid meet"
    >
      {def.shape.map((seg, i) => (
        <line
          key={i}
          x1={seg.x1} y1={seg.y1}
          x2={seg.x2} y2={seg.y2}
          stroke="#94a3b8"
          strokeWidth={0.15}
          strokeLinecap="round"
        />
      ))}
      {def.circles?.map((c, i) => (
        <circle
          key={`c-${i}`}
          cx={c.cx} cy={c.cy}
          r={c.r}
          stroke="#94a3b8"
          strokeWidth={0.15}
          fill="none"
        />
      ))}
      {def.inputs.map((pin) => (
        <circle
          key={`in-${pin.name}`}
          cx={pin.x} cy={pin.y}
          r={0.25}
          fill="#60a5fa"
        />
      ))}
      {def.outputs.map((pin) => (
        <circle
          key={`out-${pin.name}`}
          cx={pin.x} cy={pin.y}
          r={0.25}
          fill="#60a5fa"
        />
      ))}
    </svg>
  );
}
