import { useState, useEffect, useRef, useMemo } from "react";
import { GatePreview } from "./GatePreview";
import { loadedGateDefs } from "./canvas/GateLayer";
import { useCanvasStore } from "../stores/canvas-store";
import { SNAP_SIZE } from "@shared/constants";
import type { GateDefinition } from "@shared/types";

interface QuickAddDialogProps {
  onClose: () => void;
}

function snapToGrid(val: number): number {
  return Math.round(val / SNAP_SIZE) * SNAP_SIZE;
}

/** Score how well `query` matches `text`. Higher = better, -1 = no match. */
function matchScore(query: string, text: string): number {
  const q = query.toLowerCase();
  const t = text.toLowerCase();
  if (!t || !q) return -1;

  // Exact match
  if (t === q) return 100;
  // Starts with query
  if (t.startsWith(q)) return 90;
  // Word boundary match (e.g. "led" matches "Red LED")
  if (new RegExp(`\\b${q.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`).test(t))
    return 80;
  // Substring match
  if (t.includes(q)) return 70;
  // Fuzzy match — characters in order
  let qi = 0;
  for (let ti = 0; ti < t.length && qi < q.length; ti++) {
    if (t[ti] === q[qi]) qi++;
  }
  if (qi === q.length) return 50;
  return -1;
}

export function QuickAddDialog({ onClose }: QuickAddDialogProps) {
  const [query, setQuery] = useState("");
  const [selectedIndex, setSelectedIndex] = useState(0);
  const listRef = useRef<HTMLDivElement>(null);

  const filtered = useMemo(() => {
    if (!query.trim()) return loadedGateDefs;
    const q = query.trim();
    const scored: { def: GateDefinition; score: number }[] = [];
    for (const def of loadedGateDefs) {
      // Caption gets a slight bonus (+5) so display-name matches rank higher
      const caption = matchScore(q, def.caption || "");
      const id = matchScore(q, def.id);
      const lib = matchScore(q, def.library);
      const best = Math.max(
        caption >= 0 ? caption + 5 : -1,
        id,
        lib
      );
      if (best >= 0) scored.push({ def, score: best });
    }
    scored.sort((a, b) => b.score - a.score);
    return scored.map((s) => s.def);
  }, [query]);

  useEffect(() => {
    const list = listRef.current;
    if (!list) return;
    const item = list.children[selectedIndex] as HTMLElement | undefined;
    item?.scrollIntoView({ block: "nearest" });
  }, [selectedIndex]);

  function selectGate(def: GateDefinition) {
    const { viewportX, viewportY, zoom } = useCanvasStore.getState();
    const container = document.querySelector("[data-canvas-container]");
    const w = container?.clientWidth ?? window.innerWidth;
    const h = container?.clientHeight ?? window.innerHeight;
    const centerX = snapToGrid((w / 2 - viewportX) / zoom);
    const centerY = snapToGrid((h / 2 - viewportY) / zoom);

    useCanvasStore.getState().setPendingGate({
      defId: def.id,
      logicType: def.logicType || "",
      params: { ...def.params, ...def.guiParams },
      x: centerX,
      y: centerY,
    });
    onClose();
  }

  function handleKeyDown(e: React.KeyboardEvent) {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      setSelectedIndex((i) => Math.min(i + 1, filtered.length - 1));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setSelectedIndex((i) => Math.max(i - 1, 0));
    } else if (e.key === "Enter") {
      e.preventDefault();
      if (filtered[selectedIndex]) {
        selectGate(filtered[selectedIndex]);
      }
    } else if (e.key === "Escape") {
      e.preventDefault();
      onClose();
    }
  }

  return (
    <div
      className="fixed inset-0 bg-black/30 dark:bg-black/50 flex items-start justify-center z-50 pt-[15vh]"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div className="bg-gray-50 dark:bg-gray-900 rounded-xl w-full max-w-md shadow-2xl border border-gray-300 dark:border-gray-700 overflow-hidden">
        <div className="p-3 border-b border-gray-200 dark:border-gray-800">
          <input
            autoFocus
            type="text"
            placeholder="Search gates..."
            value={query}
            onChange={(e) => { setQuery(e.target.value); setSelectedIndex(0); }}
            onKeyDown={handleKeyDown}
            className="w-full bg-gray-200 dark:bg-gray-800 text-gray-900 dark:text-white rounded-lg px-3 py-2 text-sm placeholder-gray-400 dark:placeholder-gray-500 outline-none focus:ring-1 focus:ring-blue-500"
          />
        </div>
        <div ref={listRef} className="max-h-80 overflow-y-auto p-2">
          {filtered.length === 0 ? (
            <div className="text-sm text-gray-400 dark:text-gray-500 text-center py-6">No gates found</div>
          ) : (
            filtered.map((def, i) => (
              <button
                key={def.id}
                className={`w-full flex items-center gap-3 px-3 py-2 rounded-lg text-left transition-colors cursor-pointer ${
                  i === selectedIndex
                    ? "bg-blue-600/30 text-gray-900 dark:text-white"
                    : "text-gray-600 dark:text-gray-300 hover:bg-gray-200 dark:hover:bg-gray-800"
                }`}
                onMouseEnter={() => setSelectedIndex(i)}
                onClick={() => selectGate(def)}
              >
                <div className="w-10 h-10 shrink-0">
                  <GatePreview def={def} />
                </div>
                <div className="min-w-0">
                  <div className="text-sm font-medium truncate">{def.caption || def.id}</div>
                  <div className="text-xs text-gray-400 dark:text-gray-500 truncate">{def.library}</div>
                </div>
              </button>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
