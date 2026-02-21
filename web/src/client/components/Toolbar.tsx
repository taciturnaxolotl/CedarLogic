import { useEffect, useState, useCallback } from "react";
import { GatePreview } from "./GatePreview";
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

export function GatePalette() {
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
  );
}
