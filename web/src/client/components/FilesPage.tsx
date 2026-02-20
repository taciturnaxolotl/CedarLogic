import { useState, useEffect, useMemo } from "react";
import { useAuth } from "../hooks/useAuth";
import { FilesToolbar, type FilterTab, type SortBy } from "./FilesToolbar";
import { CircuitCardGrid } from "./CircuitCardGrid";
import type { FileWithPermission, ThumbnailData } from "@shared/types";

interface FilesPageProps {
  onOpenFile: (fileId: string) => void;
}

export function FilesPage({ onOpenFile }: FilesPageProps) {
  const { user, logout } = useAuth();
  const [files, setFiles] = useState<FileWithPermission[]>([]);
  const [thumbnails, setThumbnails] = useState<Record<string, ThumbnailData>>({});
  const [searchQuery, setSearchQuery] = useState("");
  const [sortBy, setSortBy] = useState<SortBy>("date");
  const [filterTab, setFilterTab] = useState<FilterTab>("all");

  useEffect(() => {
    fetchFiles();
  }, []);

  async function fetchFiles() {
    const res = await fetch("/api/files");
    if (!res.ok) return;
    const data: FileWithPermission[] = await res.json();
    setFiles(data);

    // Fetch thumbnails
    if (data.length > 0) {
      const thumbRes = await fetch("/api/files/thumbnails", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ fileIds: data.map((f) => f.id) }),
      });
      if (thumbRes.ok) {
        setThumbnails(await thumbRes.json());
      }
    }
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

  const filteredFiles = useMemo(() => {
    let result = files;

    // Filter tab
    if (filterTab === "my") {
      result = result.filter((f) => f.permission === "owner");
    } else if (filterTab === "shared") {
      result = result.filter((f) => f.permission !== "owner");
    }

    // Search
    if (searchQuery.trim()) {
      const q = searchQuery.toLowerCase();
      result = result.filter((f) => f.title.toLowerCase().includes(q));
    }

    // Sort
    result = [...result].sort((a, b) => {
      if (sortBy === "name") return a.title.localeCompare(b.title);
      if (sortBy === "owner") return (a.ownerName ?? "").localeCompare(b.ownerName ?? "");
      // date (default) — newest first
      return new Date(b.updatedAt).getTime() - new Date(a.updatedAt).getTime();
    });

    return result;
  }, [files, filterTab, searchQuery, sortBy]);

  const emptyMessage = searchQuery.trim()
    ? "No circuits match your search."
    : filterTab === "my"
      ? "You don't have any circuits yet. Create one to get started."
      : filterTab === "shared"
        ? "No one has shared any circuits with you yet."
        : "No circuits yet. Create one to get started.";

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

      <main className="max-w-6xl mx-auto px-6 py-8">
        <FilesToolbar
          filterTab={filterTab}
          onFilterTab={setFilterTab}
          searchQuery={searchQuery}
          onSearchQuery={setSearchQuery}
          sortBy={sortBy}
          onSortBy={setSortBy}
          onCreateFile={createFile}
        />

        <CircuitCardGrid
          files={filteredFiles}
          thumbnails={thumbnails}
          emptyMessage={emptyMessage}
          onOpenFile={onOpenFile}
          onDeleteFile={deleteFile}
        />
      </main>
    </div>
  );
}
