import { useState, useEffect, useRef, useCallback, useMemo } from "react";
import * as Y from "yjs";
import { getGatesMap } from "../lib/collab/yjs-schema";
import { loadedGateDefs } from "./canvas/GateLayer";

interface RamEditorDialogProps {
  gateId: string;
  doc: Y.Doc;
  onClose: () => void;
}

const COLS = 16;

export function RamEditorDialog({ gateId, doc, onClose }: RamEditorDialogProps) {
  const gatesMap = getGatesMap(doc);
  const yGate = gatesMap.get(gateId);
  const defId = yGate?.get("defId") as string | undefined;
  const def = loadedGateDefs.find((d) => d.id === defId);

  const addressBits = parseInt(yGate?.get("param:ADDRESS_BITS") ?? def?.params?.ADDRESS_BITS ?? "4", 10);
  const dataBits = parseInt(yGate?.get("param:DATA_BITS") ?? def?.params?.DATA_BITS ?? "4", 10);
  const totalAddresses = 1 << addressBits;
  const rows = totalAddresses / COLS;
  const dataMax = (1 << dataBits) - 1;
  const dataHexDigits = Math.ceil(dataBits / 4);
  const addrHexDigits = Math.ceil(addressBits / 4);

  const [memory, setMemory] = useState<Map<number, number>>(new Map());
  const [showDecimal, setShowDecimal] = useState(false);
  const [editingCell, setEditingCell] = useState<number | null>(null);
  const [editValue, setEditValue] = useState("");
  const editRef = useRef<HTMLInputElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);

  // Virtualization state for large memories
  const ROW_HEIGHT = 28;
  const VISIBLE_ROWS = 24;
  const [scrollTop, setScrollTop] = useState(0);

  // Sync memory from Yjs
  useEffect(() => {
    if (!yGate) return;

    function sync() {
      const mem = new Map<number, number>();
      for (const [k, v] of yGate!.entries()) {
        if (k.startsWith("param:Address:")) {
          const addr = parseInt(k.slice("param:Address:".length), 10);
          const val = parseInt(String(v), 10);
          if (!isNaN(addr) && !isNaN(val) && val !== 0) {
            mem.set(addr, val);
          }
        }
      }
      setMemory(mem);
    }

    sync();
    yGate.observeDeep(sync);
    return () => yGate.unobserveDeep(sync);
  }, [gateId]);

  useEffect(() => {
    function handleKey(e: KeyboardEvent) {
      if (e.key === "Escape") {
        if (editingCell !== null) {
          setEditingCell(null);
        } else {
          onClose();
        }
      }
    }
    window.addEventListener("keydown", handleKey);
    return () => window.removeEventListener("keydown", handleKey);
  }, [onClose, editingCell]);

  const writeCell = useCallback(
    (addr: number, value: number) => {
      if (!yGate) return;
      const clamped = Math.max(0, Math.min(dataMax, value));
      doc.transact(() => {
        if (clamped === 0) {
          yGate.delete(`param:Address:${addr}`);
        } else {
          yGate.set(`param:Address:${addr}`, String(clamped));
        }
      });
    },
    [yGate, doc, dataMax],
  );

  const commitEdit = useCallback(() => {
    if (editingCell === null) return;
    const parsed = showDecimal
      ? parseInt(editValue, 10)
      : parseInt(editValue, 16);
    if (!isNaN(parsed)) {
      writeCell(editingCell, parsed);
    }
    setEditingCell(null);
  }, [editingCell, editValue, showDecimal, writeCell]);

  const startEdit = useCallback(
    (addr: number) => {
      const val = memory.get(addr) ?? 0;
      setEditingCell(addr);
      setEditValue(
        showDecimal
          ? String(val)
          : val.toString(16).toUpperCase().padStart(dataHexDigits, "0"),
      );
      setTimeout(() => editRef.current?.select(), 0);
    },
    [memory, showDecimal, dataHexDigits],
  );

  const handleEditKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === "Enter") {
        commitEdit();
      } else if (e.key === "Tab" && editingCell !== null) {
        e.preventDefault();
        commitEdit();
        const next = e.shiftKey
          ? (editingCell - 1 + totalAddresses) % totalAddresses
          : (editingCell + 1) % totalAddresses;
        // Wait for state to settle then start editing next
        setTimeout(() => startEdit(next), 0);
      }
    },
    [commitEdit, editingCell, totalAddresses, startEdit],
  );

  const formatValue = useCallback(
    (val: number) => {
      if (showDecimal) return String(val);
      return val.toString(16).toUpperCase().padStart(dataHexDigits, "0");
    },
    [showDecimal, dataHexDigits],
  );

  const formatAddr = useCallback(
    (addr: number) => {
      return "0x" + addr.toString(16).toUpperCase().padStart(addrHexDigits, "0");
    },
    [addrHexDigits],
  );

  // Compute visible row range for virtualization
  const startRow = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - 2);
  const endRow = Math.min(rows, startRow + VISIBLE_ROWS + 4);

  const cellWidth = showDecimal
    ? Math.max(40, String(dataMax).length * 10 + 12)
    : dataHexDigits * 10 + 16;

  const needsVirtualization = rows > 100;

  const handleScroll = useCallback((e: React.UIEvent<HTMLDivElement>) => {
    setScrollTop(e.currentTarget.scrollTop);
  }, []);

  const clearAll = useCallback(() => {
    if (!yGate) return;
    doc.transact(() => {
      const keysToDelete: string[] = [];
      for (const [k] of yGate.entries()) {
        if (k.startsWith("param:Address:")) {
          keysToDelete.push(k);
        }
      }
      for (const k of keysToDelete) {
        yGate.delete(k);
      }
    });
  }, [yGate, doc]);

  if (!yGate || !def) return null;

  const isRom = def.id.includes("ROM");
  const caption = def.caption;

  return (
    <div
      className="fixed inset-0 bg-black/30 dark:bg-black/50 flex items-center justify-center z-50"
      onClick={onClose}
    >
      <div
        className="bg-gray-50 dark:bg-gray-900 rounded-xl p-5 flex flex-col"
        style={{ maxHeight: "85vh", maxWidth: "90vw" }}
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">{caption} Memory Editor</h3>
          <div className="flex items-center gap-3">
            <label className="flex items-center gap-1.5 text-sm text-gray-500 dark:text-gray-400 cursor-pointer select-none">
              <input
                type="checkbox"
                checked={showDecimal}
                onChange={(e) => {
                  setShowDecimal(e.target.checked);
                  setEditingCell(null);
                }}
                className="rounded border-gray-400 dark:border-gray-600 bg-gray-200 dark:bg-gray-800"
              />
              Decimal
            </label>
            <button
              onClick={clearAll}
              className="text-xs text-gray-500 dark:text-gray-400 hover:text-red-400 transition-colors cursor-pointer px-2 py-1 rounded hover:bg-gray-200 dark:hover:bg-gray-800"
            >
              Clear All
            </button>
          </div>
        </div>

        {/* Info bar */}
        <div className="text-xs text-gray-400 dark:text-gray-500 mb-2">
          {totalAddresses} addresses x {dataBits}-bit
          {memory.size > 0 && ` \u00b7 ${memory.size} non-zero cells`}
        </div>

        {/* Grid */}
        <div
          ref={scrollRef}
          className="overflow-auto flex-1 min-h-0 border border-gray-200 dark:border-gray-800 rounded"
          style={{ maxHeight: needsVirtualization ? VISIBLE_ROWS * ROW_HEIGHT + 8 : undefined }}
          onScroll={needsVirtualization ? handleScroll : undefined}
        >
          <table className="border-collapse text-xs font-mono">
            {/* Column headers */}
            <thead>
              <tr className="sticky top-0 z-10 bg-gray-200 dark:bg-gray-800">
                <th className="px-2 py-1 text-gray-400 dark:text-gray-500 text-left border-b border-gray-300 dark:border-gray-700 sticky left-0 bg-gray-200 dark:bg-gray-800 z-20" />
                {Array.from({ length: COLS }, (_, c) => (
                  <th
                    key={c}
                    className="py-1 text-gray-400 dark:text-gray-500 text-center border-b border-gray-300 dark:border-gray-700 font-normal"
                    style={{ width: cellWidth, minWidth: cellWidth }}
                  >
                    {c.toString(16).toUpperCase()}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {needsVirtualization && startRow > 0 && (
                <tr style={{ height: startRow * ROW_HEIGHT }} />
              )}
              {Array.from({ length: endRow - startRow }, (_, ri) => {
                const row = startRow + ri;
                const baseAddr = row * COLS;
                return (
                  <tr key={row} style={{ height: ROW_HEIGHT }}>
                    <td className="px-2 text-gray-400 dark:text-gray-500 whitespace-nowrap border-r border-gray-200 dark:border-gray-800 sticky left-0 bg-gray-50 dark:bg-gray-900">
                      {formatAddr(baseAddr)}
                    </td>
                    {Array.from({ length: COLS }, (_, col) => {
                      const addr = baseAddr + col;
                      const val = memory.get(addr) ?? 0;
                      const isEditing = editingCell === addr;

                      return (
                        <td
                          key={col}
                          className={`text-center cursor-pointer border border-gray-200/50 dark:border-gray-800/50 ${
                            val !== 0
                              ? "text-blue-600 dark:text-blue-300 bg-gray-200/40 dark:bg-gray-800/40"
                              : "text-gray-400 dark:text-gray-600"
                          } hover:bg-gray-300/70 dark:hover:bg-gray-700/50`}
                          style={{ width: cellWidth, minWidth: cellWidth }}
                          onClick={() => startEdit(addr)}
                        >
                          {isEditing ? (
                            <input
                              ref={editRef}
                              value={editValue}
                              onChange={(e) => setEditValue(e.target.value)}
                              onBlur={commitEdit}
                              onKeyDown={handleEditKeyDown}
                              className="w-full bg-blue-100/50 dark:bg-blue-900/50 text-gray-900 dark:text-white text-center text-xs font-mono outline-none border border-blue-500 rounded-sm"
                              style={{ padding: "1px 2px" }}
                              autoFocus
                            />
                          ) : (
                            formatValue(val)
                          )}
                        </td>
                      );
                    })}
                  </tr>
                );
              })}
              {needsVirtualization && endRow < rows && (
                <tr style={{ height: (rows - endRow) * ROW_HEIGHT }} />
              )}
            </tbody>
          </table>
        </div>

        {/* Footer */}
        <div className="flex justify-end mt-3">
          <button
            onClick={onClose}
            className="px-4 py-1.5 text-sm bg-gray-300 dark:bg-gray-700 text-gray-600 dark:text-gray-300 rounded hover:bg-gray-400 dark:hover:bg-gray-600 transition-colors cursor-pointer"
          >
            Close
          </button>
        </div>
      </div>
    </div>
  );
}
