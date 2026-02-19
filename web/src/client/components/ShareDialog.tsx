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
  const [copied, setCopied] = useState(false);

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

  function copyLink() {
    const url = `${window.location.origin}/p/${fileId}`;
    navigator.clipboard.writeText(url).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    });
  }

  if (!file) return null;

  const isPublic = file.linkSharing !== "private";

  return (
    <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50" onClick={onClose}>
      <div className="bg-gray-900 rounded-xl p-6 w-full max-w-md" onClick={(e) => e.stopPropagation()}>
        <div className="flex items-center justify-between mb-4">
          <h3 className="text-lg font-semibold text-white">Share "{file.title}"</h3>
          <button
            onClick={onClose}
            className="text-gray-400 hover:text-white cursor-pointer"
          >
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round">
              <path d="M4 4l8 8M12 4l-8 8" />
            </svg>
          </button>
        </div>

        {/* Link sharing */}
        <div className="mb-4">
          <label className="block text-sm text-gray-400 mb-2">Link access</label>
          <select
            value={file.linkSharing}
            onChange={(e) => updateLinkSharing(e.target.value)}
            className="w-full bg-gray-800 text-white rounded px-3 py-2 text-sm cursor-pointer"
          >
            <option value="private">Private - only invited people</option>
            <option value="viewer">Anyone with link can view</option>
            <option value="editor">Anyone with link can edit</option>
          </select>
        </div>

        {/* Copy link button */}
        <button
          onClick={copyLink}
          className={`w-full flex items-center justify-center gap-2 px-3 py-2 rounded text-sm font-medium transition-colors cursor-pointer mb-4 ${
            copied
              ? "bg-green-600/20 text-green-400 border border-green-600/40"
              : isPublic
                ? "bg-blue-600 hover:bg-blue-500 text-white"
                : "bg-gray-800 hover:bg-gray-700 text-gray-300"
          }`}
        >
          {copied ? (
            <>
              <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <path d="M2.5 7.5L5.5 10.5L11.5 3.5" />
              </svg>
              Copied!
            </>
          ) : (
            <>
              <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
                <rect x="5" y="5" width="7" height="7" rx="1" />
                <path d="M9 5V3a1 1 0 0 0-1-1H3a1 1 0 0 0-1 1v5a1 1 0 0 0 1 1h2" />
              </svg>
              Copy link
            </>
          )}
        </button>

        {!isPublic && (
          <p className="text-xs text-gray-500 -mt-3 mb-4">
            Only invited people can access this link. Change to "Anyone with link" above to share publicly.
          </p>
        )}

        {/* Invite by email */}
        <div className="border-t border-gray-800 pt-4">
          <label className="block text-sm text-gray-400 mb-2">Invite people</label>
          <form onSubmit={addInvite} className="flex gap-2 mb-3">
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
              className="bg-gray-800 text-white rounded px-3 py-2 text-sm cursor-pointer"
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
                      <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round">
                        <path d="M3 3l6 6M9 3l-6 6" />
                      </svg>
                    </button>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
