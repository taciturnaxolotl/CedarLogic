import { useState, useEffect } from "react";
import { useAuth } from "../hooks/useAuth";
import { ShareDialog } from "./ShareDialog";
import type { FileWithPermission } from "@shared/types";

interface FilesPageProps {
  onOpenFile: (fileId: string) => void;
}

export function FilesPage({ onOpenFile }: FilesPageProps) {
  const { user, logout } = useAuth();
  const [files, setFiles] = useState<FileWithPermission[]>([]);
  const [shareFileId, setShareFileId] = useState<string | null>(null);

  useEffect(() => {
    fetchFiles();
  }, []);

  async function fetchFiles() {
    const res = await fetch("/api/files");
    if (res.ok) setFiles(await res.json());
  }

  async function createFile() {
    const res = await fetch("/api/files", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({}),
    });
    if (res.ok) {
      const file = await res.json();
      onOpenFile(file.id);
    }
  }

  async function deleteFile(id: string, e: React.MouseEvent) {
    e.stopPropagation();
    if (!confirm("Delete this circuit?")) return;
    await fetch(`/api/files/${id}`, { method: "DELETE" });
    fetchFiles();
  }

  return (
    <div className="min-h-screen bg-gray-950 text-white">
      <header className="flex items-center justify-between px-6 py-4 border-b border-gray-800">
        <h1 className="text-xl font-bold">CedarLogic Web</h1>
        <div className="flex items-center gap-4">
          <span className="text-gray-400 text-sm">{user?.email}</span>
          <button
            onClick={logout}
            className="text-sm text-gray-400 hover:text-white transition-colors cursor-pointer"
          >
            Sign out
          </button>
        </div>
      </header>

      <main className="max-w-4xl mx-auto px-6 py-8">
        <div className="flex items-center justify-between mb-6">
          <h2 className="text-lg font-semibold">My Circuits</h2>
          <button
            onClick={createFile}
            className="px-4 py-2 bg-blue-600 rounded-lg text-sm font-medium hover:bg-blue-500 transition-colors cursor-pointer"
          >
            New Circuit
          </button>
        </div>

        {files.length === 0 ? (
          <p className="text-gray-500">No circuits yet. Create one to get started.</p>
        ) : (
          <div className="grid gap-3">
            {files.map((file) => (
              <div
                key={file.id}
                onClick={() => onOpenFile(file.id)}
                className="flex items-center justify-between p-4 bg-gray-900 rounded-lg hover:bg-gray-800 transition-colors cursor-pointer"
              >
                <div>
                  <div className="font-medium">{file.title}</div>
                  <div className="text-sm text-gray-500">
                    {file.permission === "owner" ? "Owned by you" : `Shared by ${file.ownerName}`}
                    {" · "}
                    {new Date(file.updatedAt).toLocaleDateString()}
                  </div>
                </div>
                {file.permission === "owner" && (
                  <div className="flex gap-2">
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        setShareFileId(file.id);
                      }}
                      className="px-3 py-1 text-sm bg-gray-700 rounded hover:bg-gray-600 transition-colors cursor-pointer"
                    >
                      Share
                    </button>
                    <button
                      onClick={(e) => deleteFile(file.id, e)}
                      className="px-3 py-1 text-sm bg-gray-700 rounded hover:bg-red-600 transition-colors cursor-pointer"
                    >
                      Delete
                    </button>
                  </div>
                )}
              </div>
            ))}
          </div>
        )}
      </main>

      {shareFileId && (
        <ShareDialog
          fileId={shareFileId}
          onClose={() => {
            setShareFileId(null);
            fetchFiles();
          }}
        />
      )}
    </div>
  );
}
