import { useEffect, useState, useRef, useCallback } from "react";
import { useCollab } from "../hooks/useCollab";
import { useSimulation } from "../hooks/useSimulation";
import { Canvas } from "./Canvas";
import { Toolbar, SimControls } from "./Toolbar";
import { QuickAddDialog } from "./QuickAddDialog";
import { ShareDialog } from "./ShareDialog";
import { exportToCdl } from "../lib/cdl-export";
import { importFromCdl } from "../lib/cdl-import";
import { loadedGateDefs } from "./canvas/GateLayer";
import { usePresence } from "../hooks/usePresence";
import type { FileRecord, PermissionLevel, PublicUser } from "@shared/types";

interface EditorPageProps {
  fileId: string;
  onBack: (() => void) | null;
  user?: PublicUser | null;
}

type FileState =
  | { status: "loading" }
  | { status: "denied" }
  | { status: "loaded"; file: FileRecord & { permission: PermissionLevel } };

export function EditorPage({ fileId, onBack, user: currentUser }: EditorPageProps) {
  const [fileState, setFileState] = useState<FileState>({ status: "loading" });
  const file = fileState.status === "loaded" ? fileState.file : null;

  // Only connect collab once we know we have access
  const { doc, provider, connected, synced } = useCollab(file ? fileId : null);

  useSimulation(doc);

  const permission = file?.permission ?? null;
  const { updateCursor, clearCursor, awareness, remoteUsers } = usePresence(
    provider,
    currentUser ? { name: currentUser.name, avatarUrl: currentUser.avatarUrl } : null,
    permission,
  );

  useEffect(() => {
    fetch(`/api/files/${fileId}`)
      .then((r) => {
        if (r.status === 401 || r.status === 403) {
          setFileState({ status: "denied" });
          return;
        }
        if (!r.ok) throw new Error("Failed to load file");
        return r.json();
      })
      .then((data) => {
        if (data) setFileState({ status: "loaded", file: data });
      })
      .catch(() => setFileState({ status: "denied" }));
  }, [fileId]);

  const readOnly = file ? file.permission === "viewer" : null;
  const isOwner = file?.permission === "owner";
  const [editingTitle, setEditingTitle] = useState(false);
  const [quickAddOpen, setQuickAddOpen] = useState(false);
  const [shareOpen, setShareOpen] = useState(false);
  const [presenceOpen, setPresenceOpen] = useState(false);
  const titleInputRef = useRef<HTMLInputElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const saveTitle = useCallback(
    (newTitle: string) => {
      const trimmed = newTitle.trim();
      if (!trimmed || !file || trimmed === file.title) {
        setEditingTitle(false);
        return;
      }
      setFileState({ status: "loaded", file: { ...file, title: trimmed } });
      setEditingTitle(false);
      fetch(`/api/files/${fileId}`, {
        method: "PATCH",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ title: trimmed }),
      });
    },
    [file, fileId],
  );

  if (fileState.status === "loading") {
    return (
      <div className="h-screen flex items-center justify-center bg-gray-950 text-gray-500">
        Loading...
      </div>
    );
  }

  if (fileState.status === "denied") {
    return (
      <div className="h-screen flex flex-col items-center justify-center bg-gray-950 gap-4">
        <p className="text-gray-400">You don't have access to this circuit, or it doesn't exist.</p>
        <div className="flex gap-3">
          <a
            href="/auth/google"
            className="px-4 py-2 bg-blue-600 text-white text-sm rounded-lg hover:bg-blue-500 transition-colors"
          >
            Sign in
          </a>
          {onBack && (
            <button
              onClick={onBack}
              className="px-4 py-2 bg-gray-700 text-gray-300 text-sm rounded-lg hover:bg-gray-600 transition-colors cursor-pointer"
            >
              Go back
            </button>
          )}
        </div>
      </div>
    );
  }

  // After the guards above, file is guaranteed non-null
  const { file: loadedFile } = fileState;

  return (
    <div className="h-dvh flex flex-col bg-gray-950">
      <header className="flex items-center justify-between px-4 py-2 border-b border-gray-800 shrink-0">
        <div className="flex items-center gap-3">
          {onBack && (
            <button
              onClick={onBack}
              className="text-gray-400 hover:text-white transition-colors cursor-pointer"
            >
              &larr; Back
            </button>
          )}
          {editingTitle ? (
            <input
              ref={titleInputRef}
              defaultValue={loadedFile.title}
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
              {loadedFile.title}
            </span>
          )}
          {readOnly && (
            <span className="text-xs bg-gray-700 text-gray-300 px-2 py-0.5 rounded">
              View only
            </span>
          )}
        </div>
        <div className="flex items-center gap-1">
          {!readOnly && (
            <>
              <input
                type="file"
                accept=".cdl,.CDL"
                className="hidden"
                ref={fileInputRef}
                onChange={(e) => {
                  const f = e.target.files?.[0];
                  if (!f || !doc) return;
                  const reader = new FileReader();
                  reader.onload = () => {
                    const text = reader.result as string;
                    try {
                      importFromCdl(doc, text, loadedGateDefs);
                    } catch (err) {
                      console.error("[CDL] Import error:", err);
                    }
                  };
                  reader.readAsText(f);
                }}
              />
              <button
                onClick={() => fileInputRef.current?.click()}
                className="text-gray-400 hover:text-white transition-colors cursor-pointer p-1.5 rounded hover:bg-gray-800"
                title="Import .cdl file"
              >
                <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M8 10V2m0 0L5 5m3-3 3 3" />
                  <path d="M2 10v2.5A1.5 1.5 0 0 0 3.5 14h9a1.5 1.5 0 0 0 1.5-1.5V10" />
                </svg>
              </button>
            </>
          )}
          <button
            onClick={() => doc && exportToCdl(doc, loadedFile.title)}
            className="text-gray-400 hover:text-white transition-colors cursor-pointer p-1.5 rounded hover:bg-gray-800"
            title="Export .cdl file"
          >
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
              <path d="M8 2v8m0 0 3-3M8 10 5 7" />
              <path d="M2 10v2.5A1.5 1.5 0 0 0 3.5 14h9a1.5 1.5 0 0 0 1.5-1.5V10" />
            </svg>
          </button>
          {isOwner && (
            <button
              onClick={() => setShareOpen(true)}
              className="text-gray-400 hover:text-white transition-colors cursor-pointer p-1.5 rounded hover:bg-gray-800"
              title="Share"
            >
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
                <circle cx="12" cy="3.5" r="2" />
                <circle cx="12" cy="12.5" r="2" />
                <circle cx="4" cy="8" r="2" />
                <path d="m5.8 7 4.4-2.5M5.8 9l4.4 2.5" />
              </svg>
            </button>
          )}
          {remoteUsers.length > 0 && (
            <>
              <div className="w-px h-4 bg-gray-700 mx-1" />
              <div className="relative">
                <button
                  onClick={() => setPresenceOpen(!presenceOpen)}
                  className="flex items-center -space-x-1.5 cursor-pointer hover:opacity-80 transition-opacity"
                >
                  {remoteUsers.slice(0, 5).map((u) => (
                    <div
                      key={u.clientId}
                      className="w-6 h-6 rounded-full border-2 border-gray-900 flex items-center justify-center text-[10px] font-medium shrink-0 overflow-hidden"
                      style={{ backgroundColor: u.avatarUrl ? undefined : u.color }}
                    >
                      {u.avatarUrl ? (
                        <img src={u.avatarUrl} alt={u.name} className="w-full h-full object-cover" referrerPolicy="no-referrer" />
                      ) : (
                        <span className="text-white">{u.name[0]}</span>
                      )}
                    </div>
                  ))}
                  {remoteUsers.length > 5 && (
                    <div className="w-6 h-6 rounded-full border-2 border-gray-900 bg-gray-700 flex items-center justify-center text-[10px] font-medium text-gray-300 shrink-0">
                      +{remoteUsers.length - 5}
                    </div>
                  )}
                </button>
                {presenceOpen && (
                  <>
                    <div className="fixed inset-0 z-20" onClick={() => setPresenceOpen(false)} />
                    <div className="absolute right-0 top-full mt-2 z-30 bg-gray-800 border border-gray-700 rounded-lg shadow-xl py-1 w-[240px]">
                      <div className="px-3 py-1.5 text-[11px] text-gray-500 font-medium uppercase tracking-wider">
                        Online — {remoteUsers.length + 1}
                      </div>
                      {/* Current user */}
                      <div className="flex items-center gap-2.5 px-3 py-1.5">
                        <div
                          className="w-7 h-7 rounded-full flex items-center justify-center text-xs font-medium shrink-0 overflow-hidden"
                          style={{ backgroundColor: currentUser?.avatarUrl ? undefined : "#6B7280" }}
                        >
                          {currentUser?.avatarUrl ? (
                            <img src={currentUser.avatarUrl} alt={currentUser.name} className="w-full h-full object-cover" referrerPolicy="no-referrer" />
                          ) : (
                            <span className="text-white">{(currentUser?.name ?? "A")[0]}</span>
                          )}
                        </div>
                        <div className="min-w-0 flex-1">
                          <div className="text-sm text-white truncate">
                            {currentUser?.name ?? "Anonymous"} <span className="text-gray-500 text-xs">(you)</span>
                          </div>
                        </div>
                        <span className="text-[10px] px-1.5 py-0.5 rounded bg-gray-700 text-gray-300 capitalize shrink-0">
                          {permission || "viewer"}
                        </span>
                      </div>
                      {/* Separator */}
                      <div className="border-t border-gray-700 my-1" />
                      {/* Remote users */}
                      <div className="max-h-[240px] overflow-y-auto">
                        {remoteUsers.map((u) => (
                          <div key={u.clientId} className="flex items-center gap-2.5 px-3 py-1.5">
                            <div
                              className="w-7 h-7 rounded-full flex items-center justify-center text-xs font-medium shrink-0 overflow-hidden"
                              style={{ backgroundColor: u.avatarUrl ? undefined : u.color }}
                            >
                              {u.avatarUrl ? (
                                <img src={u.avatarUrl} alt={u.name} className="w-full h-full object-cover" referrerPolicy="no-referrer" />
                              ) : (
                                <span className="text-white">{u.name[0]}</span>
                              )}
                            </div>
                            <div className="min-w-0 flex-1">
                              <div className="text-sm text-white truncate">{u.name}</div>
                            </div>
                            <div className="flex items-center gap-1.5 shrink-0">
                              <span className="text-[10px] px-1.5 py-0.5 rounded bg-gray-700 text-gray-300 capitalize">
                                {u.role || "viewer"}
                              </span>
                              <div
                                className="w-2 h-2 rounded-full"
                                style={{ backgroundColor: u.color }}
                              />
                            </div>
                          </div>
                        ))}
                      </div>
                    </div>
                  </>
                )}
              </div>
            </>
          )}
          <div className="w-px h-4 bg-gray-700 mx-1" />
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
            <Canvas
              doc={doc}
              readOnly={!!readOnly}
              onQuickAdd={() => setQuickAddOpen(true)}
              onCursorMove={updateCursor}
              onCursorLeave={clearCursor}
              awareness={awareness}
            />
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

      {quickAddOpen && (
        <QuickAddDialog onClose={() => setQuickAddOpen(false)} />
      )}
      {shareOpen && (
        <ShareDialog fileId={fileId} onClose={() => setShareOpen(false)} />
      )}
    </div>
  );
}
