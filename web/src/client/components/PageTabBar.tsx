import { useState, useEffect } from "react";
import * as Y from "yjs";
import {
  getMetaMap,
  getPageList,
  setPageList,
  getGatesMap,
  getWiresMap,
  getConnectionsMap,
  getPage,
} from "../lib/collab/yjs-schema";
import { useCanvasStore } from "../stores/canvas-store";

interface PageTabBarProps {
  doc: Y.Doc;
  readOnly: boolean;
}

export function PageTabBar({ doc, readOnly }: PageTabBarProps) {
  const [pages, setPages] = useState<string[]>(["0"]);
  const activePage = useCanvasStore((s) => s.activePage);
  const setActivePage = useCanvasStore((s) => s.setActivePage);

  useEffect(() => {
    const meta = getMetaMap(doc);

    function sync() {
      setPages(getPageList(doc));
    }

    sync();
    meta.observeDeep(sync);
    return () => meta.unobserveDeep(sync);
  }, [doc]);

  const handleAddPage = () => {
    const maxNum = pages.reduce((max, p) => Math.max(max, parseInt(p, 10) || 0), 0);
    const newPage = String(maxNum + 1);
    const newPages = [...pages, newPage];
    setPageList(doc, newPages);
    setActivePage(newPage);
  };

  const handleDeletePage = (page: string) => {
    if (pages.length <= 1) return;
    if (!window.confirm(`Delete page ${page}? All gates and wires on this page will be removed.`)) return;

    doc.transact(() => {
      const gates = getGatesMap(doc);
      const wires = getWiresMap(doc);
      const connections = getConnectionsMap(doc);

      // Collect IDs to delete
      const gateIds = new Set<string>();
      const wireIds = new Set<string>();

      gates.forEach((yGate, id) => {
        if (getPage(yGate) === page) gateIds.add(id);
      });
      wires.forEach((yWire, id) => {
        if (getPage(yWire) === page) wireIds.add(id);
      });

      // Delete gates and wires
      for (const id of gateIds) gates.delete(id);
      for (const id of wireIds) wires.delete(id);

      // Delete connections referencing deleted gates/wires
      const connKeysToDelete: string[] = [];
      connections.forEach((yConn, key) => {
        const gateId = yConn.get("gateId") as string;
        const wireId = yConn.get("wireId") as string;
        if (gateIds.has(gateId) || wireIds.has(wireId)) {
          connKeysToDelete.push(key);
        }
      });
      for (const key of connKeysToDelete) connections.delete(key);

      // Update page list
      const newPages = pages.filter((p) => p !== page);
      setPageList(doc, newPages);
    });

    // Switch to another page if we deleted the active one
    if (activePage === page) {
      const remaining = pages.filter((p) => p !== page);
      setActivePage(remaining[0] ?? "0");
    }
  };

  return (
    <div className="flex items-center gap-0.5 px-2 h-8 bg-gray-100 dark:bg-gray-900 border-t border-gray-200 dark:border-gray-800 select-none overflow-x-auto">
      {pages.map((page) => (
        <div
          key={page}
          className={`flex items-center gap-1 px-3 h-6 rounded text-xs cursor-pointer transition-colors ${
            page === activePage
              ? "bg-white dark:bg-gray-700 text-gray-900 dark:text-gray-100 shadow-sm"
              : "text-gray-500 dark:text-gray-400 hover:bg-gray-200 dark:hover:bg-gray-800"
          }`}
          onClick={() => setActivePage(page)}
        >
          <span>Page {page}</span>
          {!readOnly && pages.length > 1 && (
            <button
              className="ml-1 text-gray-400 hover:text-red-500 dark:hover:text-red-400 text-xs leading-none"
              onClick={(e) => {
                e.stopPropagation();
                handleDeletePage(page);
              }}
            >
              ×
            </button>
          )}
        </div>
      ))}
      {!readOnly && (
        <button
          className="flex items-center justify-center w-6 h-6 rounded text-gray-400 hover:bg-gray-200 dark:hover:bg-gray-800 hover:text-gray-600 dark:hover:text-gray-300 text-sm cursor-pointer transition-colors"
          onClick={handleAddPage}
          title="Add page"
        >
          +
        </button>
      )}
    </div>
  );
}
