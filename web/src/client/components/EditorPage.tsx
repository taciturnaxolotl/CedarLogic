import { useEffect, useState, useRef, useCallback } from "react";
import { useCollab } from "../hooks/useCollab";
import { useSimulation } from "../hooks/useSimulation";
import { Canvas } from "./Canvas";
import { Toolbar, SimControls } from "./Toolbar";
import type { FileRecord, PermissionLevel } from "@shared/types";

interface EditorPageProps {
  fileId: string;
  onBack: () => void;
}

export function EditorPage({ fileId, onBack }: EditorPageProps) {
  const { doc, provider, connected, synced } = useCollab(fileId);
  const [file, setFile] = useState<(FileRecord & { permission: PermissionLevel }) | null>(null);

  useSimulation(doc);

  useEffect(() => {
    fetch(`/api/files/${fileId}`)
      .then((r) => r.json())
      .then(setFile);
  }, [fileId]);

  const readOnly = file ? file.permission === "viewer" : null;
  const isOwner = file?.permission === "owner";
  const [editingTitle, setEditingTitle] = useState(false);
  const titleInputRef = useRef<HTMLInputElement>(null);

  const saveTitle = useCallback(
    (newTitle: string) => {
      const trimmed = newTitle.trim();
      if (!trimmed || !file || trimmed === file.title) {
        setEditingTitle(false);
        return;
      }
      setFile({ ...file, title: trimmed });
      setEditingTitle(false);
      fetch(`/api/files/${fileId}`, {
        method: "PATCH",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ title: trimmed }),
      });
    },
    [file, fileId],
  );

  if (!file) {
    return (
      <div className="h-screen flex items-center justify-center bg-gray-950 text-gray-500">
        Loading...
      </div>
    );
  }

  return (
    <div className="h-screen flex flex-col bg-gray-950">
      <header className="flex items-center justify-between px-4 py-2 border-b border-gray-800 shrink-0">
        <div className="flex items-center gap-3">
          <button
            onClick={onBack}
            className="text-gray-400 hover:text-white transition-colors cursor-pointer"
          >
            &larr; Back
          </button>
          {editingTitle ? (
            <input
              ref={titleInputRef}
              defaultValue={file.title}
              className="text-white font-medium bg-gray-800 border border-gray-600 rounded px-2 py-0.5 outline-none focus:border-blue-500"
              autoFocus
              onBlur={(e) => saveTitle(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") saveTitle(e.currentTarget.value);
                if (e.key === "Escape") setEditingTitle(false);
              }}
            />
          ) : (
            <span
              className={`text-white font-medium ${isOwner ? "cursor-pointer hover:text-blue-400 transition-colors" : ""}`}
              onClick={() => isOwner && setEditingTitle(true)}
            >
              {file.title}
            </span>
          )}
          {readOnly && (
            <span className="text-xs bg-gray-700 text-gray-300 px-2 py-0.5 rounded">
              View only
            </span>
          )}
        </div>
        <div className="flex items-center gap-2">
          <span
            className={`w-2.5 h-2.5 rounded-full ${connected ? "bg-green-400" : "bg-red-400"}`}
            title={connected ? "Connected" : "Disconnected"}
          />
        </div>
      </header>

      <div className="flex flex-1 min-h-0">
        {!readOnly && <Toolbar />}
        <div className="flex-1 relative">
          {doc && synced ? (
            <Canvas doc={doc} readOnly={!!readOnly} />
          ) : (
            <div className="flex items-center justify-center h-full text-gray-500">
              Syncing...
            </div>
          )}
          {/* Floating simulation controls */}
          <div className="absolute top-3 left-1/2 -translate-x-1/2 z-10">
            <SimControls />
          </div>
        </div>
      </div>
    </div>
  );
}
