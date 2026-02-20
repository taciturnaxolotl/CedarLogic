import { useEffect, useState, useCallback } from "react";
import { useSimulationStore } from "../stores/simulation-store";
import { GatePreview } from "./GatePreview";
import type { GateDefinition } from "@shared/types";

// Log-scale slider: maps a 0–1 slider position to 1–300 Hz exponentially.
// This gives equal travel per doubling (e.g. 1→2→4→8→16→…→256→300).
const MIN_HZ = 1;
const MAX_HZ = 100;
const LOG_MIN = Math.log(MIN_HZ);
const LOG_MAX = Math.log(MAX_HZ);

function sliderToHz(t: number): number {
  return Math.round(Math.exp(LOG_MIN + t * (LOG_MAX - LOG_MIN)));
}

function hzToSlider(hz: number): number {
  return (Math.log(hz) - LOG_MIN) / (LOG_MAX - LOG_MIN);
}

export function SimControls() {
  const running = useSimulationStore((s) => s.running);
  const setRunning = useSimulationStore((s) => s.setRunning);
  const stepsPerFrame = useSimulationStore((s) => s.stepsPerFrame);
  const setStepsPerFrame = useSimulationStore((s) => s.setStepsPerFrame);

  return (
    <div className={`flex items-center gap-3 backdrop-blur-sm border rounded-lg px-3 py-1.5 shadow-lg transition-colors ${
      running
        ? "bg-green-100/80 dark:bg-green-950/80 border-green-500/60 dark:border-green-700/60"
        : "bg-yellow-100/80 dark:bg-yellow-950/80 border-yellow-500/60 dark:border-yellow-700/60"
    }`}>
      <button
        onClick={() => setRunning(!running)}
        className="w-7 h-7 flex items-center justify-center rounded hover:bg-black/10 dark:hover:bg-white/10 transition-colors cursor-pointer"
        title={running ? "Pause" : "Play"}
      >
        {running ? (
          <svg width="14" height="14" viewBox="0 0 14 14" fill="currentColor" className="text-green-400">
            <rect x="2" y="1" width="3.5" height="12" rx="0.5" />
            <rect x="8.5" y="1" width="3.5" height="12" rx="0.5" />
          </svg>
        ) : (
          <svg width="14" height="14" viewBox="0 0 14 14" fill="currentColor" className="text-yellow-400">
            <path d="M3 1.5v11l9-5.5z" />
          </svg>
        )}
      </button>
      <div className="w-px h-4 bg-gray-300 dark:bg-gray-700" />
      <label className="flex items-center gap-2 text-xs text-gray-500 dark:text-gray-400">
        Speed
        <input
          type="range"
          min={0}
          max={1}
          step={0.001}
          value={hzToSlider(stepsPerFrame)}
          onChange={(e) => setStepsPerFrame(sliderToHz(Number(e.target.value)))}
          className="w-20 h-1 accent-blue-500 cursor-pointer"
        />
        <span className="text-gray-400 dark:text-gray-500 text-right tabular-nums whitespace-nowrap">{stepsPerFrame} Hz</span>
      </label>
    </div>
  );
}

function groupByLibrary(defs: GateDefinition[]): Map<string, GateDefinition[]> {
  const groups = new Map<string, GateDefinition[]>();
  for (const def of defs) {
    const lib = def.library;
    if (!groups.has(lib)) groups.set(lib, []);
    groups.get(lib)!.push(def);
  }
  return groups;
}

export function Toolbar() {
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
    <div className="w-60 bg-gray-50 dark:bg-gray-900 border-r border-gray-200 dark:border-gray-800 flex flex-col overflow-hidden">
      {/* Gate palette */}
      <div className="flex-1 overflow-y-auto p-3">
        <div className="text-xs text-gray-400 dark:text-gray-500 mb-2 uppercase tracking-wider">Gates</div>
        {Array.from(libraries.entries()).map(([lib, defs]) => (
          <div key={lib} className="mb-1">
            <button
              onClick={() => setExpandedLib(expandedLib === lib ? null : lib)}
              className="w-full text-left text-xs text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white px-2 py-1 rounded hover:bg-gray-200 dark:hover:bg-gray-800 cursor-pointer transition-colors"
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
                    className="flex flex-col items-center gap-1 p-2 rounded bg-gray-200/50 dark:bg-gray-800/50 hover:bg-gray-300/70 dark:hover:bg-gray-700/70 cursor-grab active:cursor-grabbing transition-colors border border-transparent hover:border-gray-400 dark:hover:border-gray-600"
                    title={def.caption}
                  >
                    <div className="w-10 h-10">
                      <GatePreview def={def} />
                    </div>
                    <span className="text-[10px] text-gray-500 dark:text-gray-400 text-center leading-tight truncate w-full">
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
