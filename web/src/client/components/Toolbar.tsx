import { useEffect, useState, useCallback } from "react";
import { useSimulationStore } from "../stores/simulation-store";
import type { GateDefinition } from "@shared/types";

function groupByLibrary(defs: GateDefinition[]): Map<string, GateDefinition[]> {
  const groups = new Map<string, GateDefinition[]>();
  for (const def of defs) {
    const lib = def.library;
    if (!groups.has(lib)) groups.set(lib, []);
    groups.get(lib)!.push(def);
  }
  return groups;
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
  return { minX, minY, maxX, maxY, width: maxX - minX, height: maxY - minY };
}

function GatePreview({ def }: { def: GateDefinition }) {
  const bounds = getGateBounds(def);
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

export function Toolbar() {
  const { running, setRunning, simTime, stepsPerFrame } = useSimulationStore();
  const [gateDefs, setGateDefs] = useState<GateDefinition[]>([]);
  const [expandedLib, setExpandedLib] = useState<string | null>(null);

  useEffect(() => {
    import("../lib/canvas/gate-defs.json")
      .then((mod) => setGateDefs(mod.default as unknown as GateDefinition[]))
      .catch(() => {});
  }, []);

  const handleDragStart = useCallback(
    (e: React.DragEvent, def: GateDefinition) => {
      e.dataTransfer.setData("application/cedarlogic-gate", JSON.stringify({
        defId: def.id,
        logicType: def.logicType,
        params: def.params,
      }));
      e.dataTransfer.effectAllowed = "copy";
    },
    []
  );

  const libraries = groupByLibrary(gateDefs);

  return (
    <div className="w-60 bg-gray-900 border-r border-gray-800 flex flex-col overflow-hidden">
      {/* Simulation controls */}
      <div className="p-3 border-b border-gray-800">
        <div className="text-xs text-gray-500 mb-2 uppercase tracking-wider">Simulation</div>
        <div className="flex items-center gap-2 mb-2">
          <button
            onClick={() => setRunning(!running)}
            className={`flex-1 px-2 py-1.5 text-xs rounded cursor-pointer transition-colors ${
              running ? "bg-red-600 text-white" : "bg-green-600 text-white"
            }`}
          >
            {running ? "Stop" : "Run"}
          </button>
        </div>
        <div className="text-xs text-gray-500">
          Time: {simTime} · Speed: {stepsPerFrame}
        </div>
      </div>

      {/* Gate palette */}
      <div className="flex-1 overflow-y-auto p-3">
        <div className="text-xs text-gray-500 mb-2 uppercase tracking-wider">Gates</div>
        {Array.from(libraries.entries()).map(([lib, defs]) => (
          <div key={lib} className="mb-1">
            <button
              onClick={() => setExpandedLib(expandedLib === lib ? null : lib)}
              className="w-full text-left text-xs text-gray-400 hover:text-white px-2 py-1 rounded hover:bg-gray-800 cursor-pointer transition-colors"
            >
              {expandedLib === lib ? "▾" : "▸"} {lib}
            </button>
            {expandedLib === lib && (
              <div className="grid grid-cols-2 gap-1.5 mt-1.5 px-1">
                {defs.map((def) => (
                  <div
                    key={def.id}
                    draggable
                    onDragStart={(e) => handleDragStart(e, def)}
                    className="flex flex-col items-center gap-1 p-2 rounded bg-gray-800/50 hover:bg-gray-700/70 cursor-grab active:cursor-grabbing transition-colors border border-transparent hover:border-gray-600"
                    title={def.caption}
                  >
                    <div className="w-10 h-10">
                      <GatePreview def={def} />
                    </div>
                    <span className="text-[10px] text-gray-400 text-center leading-tight truncate w-full">
                      {def.caption || def.id}
                    </span>
                  </div>
                ))}
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}
