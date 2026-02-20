import { useState, useEffect, useRef } from "react";
import * as Y from "yjs";
import { getGatesMap } from "../lib/collab/yjs-schema";
import { loadedGateDefs } from "./canvas/GateLayer";
import type { ParamDlgEntry } from "@shared/types";

interface GatePropertiesDialogProps {
  gateId: string;
  doc: Y.Doc;
  onClose: () => void;
}

function parseVarname(varname: string): { scope: "LOGIC" | "GUI"; key: string } {
  const parts = varname.split(" ");
  return { scope: parts[0] as "LOGIC" | "GUI", key: parts.slice(1).join(" ") };
}

function getParamKey(entry: ParamDlgEntry): string {
  const { key } = parseVarname(entry.varname);
  return key;
}

function readParam(yGate: Y.Map<any>, entry: ParamDlgEntry): string {
  const key = getParamKey(entry);
  const val = yGate.get(`param:${key}`);
  return val != null ? String(val) : "";
}

export function GatePropertiesDialog({ gateId, doc, onClose }: GatePropertiesDialogProps) {
  const gatesMap = getGatesMap(doc);
  const yGate = gatesMap.get(gateId);
  const defId = yGate?.get("defId") as string | undefined;
  const def = loadedGateDefs.find((d) => d.id === defId);
  const entries = def?.paramDlg?.filter((e) => e.type !== "FILE_IN" && e.type !== "FILE_OUT") ?? [];

  const [values, setValues] = useState<Record<string, string>>({});
  const firstInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!yGate) return;
    const initial: Record<string, string> = {};
    for (const entry of entries) {
      initial[getParamKey(entry)] = readParam(yGate, entry);
    }
    setValues(initial);
  }, [gateId]);

  useEffect(() => {
    firstInputRef.current?.focus();
  }, []);

  useEffect(() => {
    function handleKey(e: KeyboardEvent) {
      if (e.key === "Escape") onClose();
    }
    window.addEventListener("keydown", handleKey);
    return () => window.removeEventListener("keydown", handleKey);
  }, [onClose]);

  if (!yGate || !def || entries.length === 0) {
    return null;
  }

  function handleSave() {
    if (!yGate) return;
    doc.transact(() => {
      for (const entry of entries) {
        const key = getParamKey(entry);
        const raw = values[key] ?? "";
        let val: string = raw;
        if (entry.type === "BOOL") {
          val = raw === "true" ? "true" : "false";
        } else if (entry.type === "INT") {
          const n = parseInt(raw, 10);
          if (!isNaN(n)) {
            if (entry.range) {
              const [min, max] = entry.range.split(",").map(Number);
              val = String(Math.max(min, Math.min(max, n)));
            } else {
              val = String(n);
            }
          }
        } else if (entry.type === "FLOAT") {
          const n = parseFloat(raw);
          if (!isNaN(n)) {
            if (entry.range) {
              const [min, max] = entry.range.split(",").map(Number);
              val = String(Math.max(min, Math.min(max, n)));
            } else {
              val = String(n);
            }
          }
        }
        yGate!.set(`param:${key}`, val);
      }
    });
    onClose();
  }

  function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    handleSave();
  }

  return (
    <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50" onClick={onClose}>
      <div className="bg-gray-900 rounded-xl p-6 w-full max-w-sm" onClick={(e) => e.stopPropagation()}>
        <h3 className="text-lg font-semibold text-white mb-4">{def.caption} Properties</h3>
        <form onSubmit={handleSubmit} className="space-y-3">
          {entries.map((entry, i) => {
            const key = getParamKey(entry);
            const val = values[key] ?? "";
            const range = entry.range ? entry.range.split(",").map(Number) : null;

            if (entry.type === "BOOL") {
              return (
                <label key={key} className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
                  <input
                    type="checkbox"
                    checked={val === "true"}
                    onChange={(e) => setValues({ ...values, [key]: e.target.checked ? "true" : "false" })}
                    className="rounded border-gray-600 bg-gray-800 text-blue-500 focus:ring-blue-500"
                  />
                  {entry.label}
                </label>
              );
            }

            return (
              <div key={key}>
                <label className="block text-sm text-gray-400 mb-1">{entry.label}</label>
                <input
                  ref={i === 0 ? firstInputRef : undefined}
                  type={entry.type === "STRING" ? "text" : "number"}
                  value={val}
                  onChange={(e) => setValues({ ...values, [key]: e.target.value })}
                  step={entry.type === "FLOAT" ? 0.05 : 1}
                  min={range ? range[0] : undefined}
                  max={range ? range[1] : undefined}
                  className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-1.5 text-white text-sm outline-none focus:border-blue-500"
                />
              </div>
            );
          })}
          <div className="flex justify-end gap-2 pt-2">
            <button
              type="button"
              onClick={onClose}
              className="px-3 py-1.5 text-sm text-gray-400 hover:text-white transition-colors cursor-pointer"
            >
              Cancel
            </button>
            <button
              type="submit"
              className="px-4 py-1.5 text-sm bg-blue-600 text-white rounded hover:bg-blue-500 transition-colors cursor-pointer"
            >
              Save
            </button>
          </div>
        </form>
      </div>
    </div>
  );
}
