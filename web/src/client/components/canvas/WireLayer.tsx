import { useEffect, useState } from "react";
import { Layer, Line } from "react-konva";
import * as Y from "yjs";
import { getWiresMap } from "../../lib/collab/yjs-schema";
import { useSimulationStore } from "../../stores/simulation-store";
import { WIRE_COLORS, WIRE_STATE, GRID_SIZE } from "@shared/constants";
import type { WireSegment } from "@shared/types";

interface WireLayerProps {
  doc: Y.Doc;
}

interface WireRenderData {
  id: string;
  segments: WireSegment[];
}

export function WireLayer({ doc }: WireLayerProps) {
  const [wires, setWires] = useState<Map<string, WireRenderData>>(new Map());
  const wireStates = useSimulationStore((s) => s.wireStates);

  useEffect(() => {
    const wiresMap = getWiresMap(doc);

    function sync() {
      const next = new Map<string, WireRenderData>();
      wiresMap.forEach((yWire, id) => {
        try {
          const segments = JSON.parse(yWire.get("segments") || "[]");
          next.set(id, { id, segments });
        } catch {
          // Skip malformed wires
        }
      });
      setWires(next);
    }

    sync();
    wiresMap.observeDeep(sync);
    return () => wiresMap.unobserveDeep(sync);
  }, [doc]);

  return (
    <Layer>
      {Array.from(wires.values()).map((wire) => {
        const state = wireStates.get(wire.id) ?? WIRE_STATE.UNKNOWN;
        const color = WIRE_COLORS[state];

        return wire.segments.map((seg, i) => (
          <Line
            key={`${wire.id}-${i}`}
            points={[seg.x1, seg.y1, seg.x2, seg.y2]}
            stroke={color}
            strokeWidth={2}
            lineCap="round"
          />
        ));
      })}
    </Layer>
  );
}
