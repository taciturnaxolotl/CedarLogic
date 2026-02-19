import { useState, useEffect } from "react";
import type { FileRecord, FilePermission } from "@shared/types";

interface ShareDialogProps {
  fileId: string;
  onClose: () => void;
}

export function ShareDialog({ fileId, onClose }: ShareDialogProps) {
  const [file, setFile] = useState<(FileRecord & { permissions: FilePermission[] }) | null>(null);
  const [email, setEmail] = useState("");
  const [role, setRole] = useState<"viewer" | "editor">("viewer");

  useEffect(() => {
    fetchFile();
  }, [fileId]);

  async function fetchFile() {
    const res = await fetch(`/api/files/${fileId}`);
    if (res.ok) setFile(await res.json());
  }

  async function updateLinkSharing(linkSharing: string) {
    await fetch(`/api/files/${fileId}`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ linkSharing }),
    });
    fetchFile();
  }

  async function addInvite(e: React.FormEvent) {
    e.preventDefault();
    if (!email) return;
    await fetch(`/api/files/${fileId}/permissions`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, role }),
    });
    setEmail("");
    fetchFile();
  }

  async function removeInvite(permId: string) {
    await fetch(`/api/files/${fileId}/permissions/${permId}`, {
      method: "DELETE",
    });
    fetchFile();
  }

  if (!file) return null;

  return (
    <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div className="bg-gray-900 rounded-xl p-6 w-full max-w-md">
        <div className="flex items-center justify-between mb-4">
          <h3 className="text-lg font-semibold text-white">Share "{file.title}"</h3>
          <button
            onClick={onClose}
            className="text-gray-400 hover:text-white cursor-pointer"
          >
            ✕
          </button>
        </div>

        <div className="mb-4">
          <label className="block text-sm text-gray-400 mb-2">Link sharing</label>
          <select
            value={file.linkSharing}
            onChange={(e) => updateLinkSharing(e.target.value)}
            className="w-full bg-gray-800 text-white rounded px-3 py-2 text-sm"
          >
            <option value="private">Private - only invited people</option>
            <option value="viewer">Anyone with link can view</option>
            <option value="editor">Anyone with link can edit</option>
          </select>
        </div>

        <form onSubmit={addInvite} className="flex gap-2 mb-4">
          <input
            type="email"
            placeholder="Email address"
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            className="flex-1 bg-gray-800 text-white rounded px-3 py-2 text-sm placeholder-gray-500"
          />
          <select
            value={role}
            onChange={(e) => setRole(e.target.value as "viewer" | "editor")}
            className="bg-gray-800 text-white rounded px-3 py-2 text-sm"
          >
            <option value="viewer">Viewer</option>
            <option value="editor">Editor</option>
          </select>
          <button
            type="submit"
            className="px-3 py-2 bg-blue-600 text-sm rounded hover:bg-blue-500 transition-colors cursor-pointer"
          >
            Invite
          </button>
        </form>

        {file.permissions.length > 0 && (
          <div className="space-y-2">
            {file.permissions.map((perm) => (
              <div
                key={perm.id}
                className="flex items-center justify-between text-sm text-gray-300"
              >
                <span>{perm.userEmail}</span>
                <div className="flex items-center gap-2">
                  <span className="text-gray-500">{perm.role}</span>
                  <button
                    onClick={() => removeInvite(perm.id)}
                    className="text-gray-500 hover:text-red-400 cursor-pointer"
                  >
                    ✕
                  </button>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
