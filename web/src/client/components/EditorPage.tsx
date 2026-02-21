import { useEffect, useState, useRef, useCallback } from "react";
import { useCollab } from "../hooks/useCollab";
import { useSimulation } from "../hooks/useSimulation";
import { Canvas } from "./Canvas";
import { QuickAddDialog } from "./QuickAddDialog";
import { ShareDialog } from "./ShareDialog";
import { GatePropertiesDialog } from "./GatePropertiesDialog";
import { RamEditorDialog } from "./RamEditorDialog";
import { FloatingToolbar } from "./FloatingToolbar";
import { exportToCdl } from "../lib/cdl-export";
import { importFromCdl } from "../lib/cdl-import";
import { loadedGateDefs } from "./canvas/GateLayer";
import { getGatesMap } from "../lib/collab/yjs-schema";
import { usePresence } from "../hooks/usePresence";
import { CursorWS } from "../lib/collab/cursor-ws";
import { hashUserId } from "@/server/cursor/protocol";
import { useCanvasStore } from "../stores/canvas-store";
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
  const userId = currentUser?.id ?? "anonymous";
  const { remoteUsers, userMetaByHash } = usePresence(
    provider,
    currentUser ? { name: currentUser.name, avatarUrl: currentUser.avatarUrl } : null,
    userId,
    permission,
  );

  // Dedicated cursor WebSocket
  const [cursorWS, setCursorWS] = useState<CursorWS | null>(null);
  const cursorWSRef = useRef<CursorWS | null>(null);
  useEffect(() => {
    if (!file) return;
    let destroyed = false;

    async function initCursorWS() {
      let token = "";
      try {
        const res = await fetch("/api/auth/ws-token", { method: "POST" });
        if (res.ok) token = (await res.json()).token;
      } catch { /* anonymous */ }

      if (destroyed) return;
      const finalToken = token || `public:${fileId}`;
      const userHash = hashUserId(userId);
      const ws = new CursorWS(fileId, finalToken, userHash);
      cursorWSRef.current = ws;
      setCursorWS(ws);
    }

    initCursorWS();
    return () => {
      destroyed = true;
      cursorWSRef.current?.destroy();
      cursorWSRef.current = null;
      setCursorWS(null);
    };
  }, [file, fileId, userId]);

  // Report viewport changes to cursor server
  const viewportX = useCanvasStore((s) => s.viewportX);
  const viewportY = useCanvasStore((s) => s.viewportY);
  const zoom = useCanvasStore((s) => s.zoom);
  const canvasSize = useCanvasStore((s) => s.canvasSize);

  useEffect(() => {
    if (!cursorWSRef.current) return;
    const minX = -viewportX / zoom;
    const minY = -viewportY / zoom;
    const maxX = minX + canvasSize.width / zoom;
    const maxY = minY + canvasSize.height / zoom;
    cursorWSRef.current.sendViewportUpdate(minX, minY, maxX, maxY);
  }, [viewportX, viewportY, zoom, canvasSize]);

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
  const [quickAddOpen, setQuickAddOpen] = useState(false);
  const [shareOpen, setShareOpen] = useState(false);
  const [editingGateId, setEditingGateId] = useState<string | null>(null);
  const [ramEditorGateId, setRamEditorGateId] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const saveTitle = useCallback(
    (newTitle: string) => {
      if (!file) return;
      setFileState({ status: "loaded", file: { ...file, title: newTitle } });
      fetch(`/api/files/${fileId}`, {
        method: "PATCH",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ title: newTitle }),
      });
    },
    [file, fileId],
  );

  if (fileState.status === "loading") {
    return (
      <div className="h-screen flex items-center justify-center bg-white dark:bg-gray-950 text-gray-400 dark:text-gray-500">
        Loading...
      </div>
    );
  }

  if (fileState.status === "denied") {
    return (
      <div className="h-screen flex flex-col items-center justify-center bg-white dark:bg-gray-950 gap-4">
        <p className="text-gray-500 dark:text-gray-400">You don't have access to this circuit, or it doesn't exist.</p>
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
              className="px-4 py-2 bg-gray-300 dark:bg-gray-700 text-gray-600 dark:text-gray-300 text-sm rounded-lg hover:bg-gray-400 dark:hover:bg-gray-600 transition-colors cursor-pointer"
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
    <div className="h-dvh relative bg-white dark:bg-gray-950">
      {/* Canvas — fills entire viewport */}
      <div className="absolute inset-0">
        {doc && synced ? (
          <Canvas
            doc={doc}
            readOnly={!!readOnly}
            onQuickAdd={() => setQuickAddOpen(true)}
            onGateDblClick={(gateId) => {
              if (readOnly) return;
              const yGate = getGatesMap(doc).get(gateId);
              if (!yGate) return;
              const def = loadedGateDefs.find((d) => d.id === yGate.get("defId"));
              if (!def) return;
              if (def.logicType === "RAM") {
                setRamEditorGateId(gateId);
              } else if (def.paramDlg?.some((e) => e.type !== "FILE_IN" && e.type !== "FILE_OUT")) {
                setEditingGateId(gateId);
              }
            }}
            onCursorMove={(x, y) => cursorWSRef.current?.sendCursorMove(x, y)}
            onCursorLeave={() => cursorWSRef.current?.sendCursorLeave()}
            cursorWS={cursorWS}
            userMetaByHash={userMetaByHash}
          />
        ) : (
          <div className="flex items-center justify-center h-full text-gray-400 dark:text-gray-500">
            Syncing...
          </div>
        )}
      </div>

      {/* Floating toolbar (includes gate palette) */}
      <FloatingToolbar
        file={loadedFile}
        onBack={onBack}
        onTitleSave={saveTitle}
        connected={connected}
        readOnly={!!readOnly}
        isOwner={isOwner}
        currentUser={currentUser}
        permission={permission}
        remoteUsers={remoteUsers}
        onShareOpen={() => setShareOpen(true)}
        onImportClick={() => fileInputRef.current?.click()}
        onExport={() => doc && exportToCdl(doc, loadedFile.title)}
      />

      {/* Hidden file input for import */}
      {!readOnly && (
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
              const hasGates = getGatesMap(doc).size > 0;
              if (hasGates && !window.confirm("This circuit already has gates. Importing will add to the existing circuit. Continue?")) {
                return;
              }
              try {
                importFromCdl(doc, text, loadedGateDefs);
              } catch (err) {
                console.error("[CDL] Import error:", err);
              }
            };
            reader.readAsText(f);
            e.target.value = "";
          }}
        />
      )}

      {/* Modal dialogs */}
      {quickAddOpen && (
        <QuickAddDialog onClose={() => setQuickAddOpen(false)} />
      )}
      {shareOpen && (
        <ShareDialog fileId={fileId} onClose={() => setShareOpen(false)} />
      )}
      {editingGateId && doc && (
        <GatePropertiesDialog gateId={editingGateId} doc={doc} onClose={() => setEditingGateId(null)} />
      )}
      {ramEditorGateId && doc && (
        <RamEditorDialog gateId={ramEditorGateId} doc={doc} onClose={() => setRamEditorGateId(null)} />
      )}
    </div>
  );
}
