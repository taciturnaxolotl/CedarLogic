import { useEffect, useState } from "react";
import { useCollab } from "../hooks/useCollab";
import { useSimulation } from "../hooks/useSimulation";
import { Canvas } from "./Canvas";
import { Toolbar } from "./Toolbar";
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
          <span className="text-white font-medium">
            {file.title}
          </span>
          {readOnly && (
            <span className="text-xs bg-gray-700 text-gray-300 px-2 py-0.5 rounded">
              View only
            </span>
          )}
        </div>
        <div className="flex items-center gap-2 text-sm">
          <span className={connected ? "text-green-400" : "text-red-400"}>
            {connected ? "Connected" : "Disconnected"}
          </span>
        </div>
      </header>

      <div className="flex flex-1 min-h-0">
        {!readOnly && <Toolbar />}
        <div className="flex-1">
          {doc && synced ? (
            <Canvas doc={doc} readOnly={!!readOnly} />
          ) : (
            <div className="flex items-center justify-center h-full text-gray-500">
              Syncing...
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
