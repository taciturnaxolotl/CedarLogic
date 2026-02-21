import { useState, useEffect, useMemo } from "react";
import { useAuth } from "../hooks/useAuth";
import { useThemeStore } from "../stores/theme-store";
import { FilesToolbar, type FilterTab, type SortBy } from "./FilesToolbar";
import { CircuitCardGrid } from "./CircuitCardGrid";
import type { FileWithPermission, ThumbnailData } from "@shared/types";

interface FilesPageProps {
  onOpenFile: (fileId: string) => void;
}

export function FilesPage({ onOpenFile }: FilesPageProps) {
  const { user, logout } = useAuth();
  const { theme, toggle: toggleTheme } = useThemeStore();
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
    <div className="min-h-screen bg-white dark:bg-gray-950 text-gray-900 dark:text-white">
      <header className="flex items-center justify-between px-6 py-4">
        <div className="flex items-center gap-2.5">
          <img src="/icon.png" alt="CedarLogic" className="w-6 h-6" />
          <span className="text-lg font-bold">CedarLogic</span>
        </div>
        <div className="flex items-center gap-4">
          <button
            onClick={toggleTheme}
            className="text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white transition-colors cursor-pointer p-1.5 rounded hover:bg-gray-200 dark:hover:bg-gray-800"
            title={theme === "dark" ? "Switch to light mode" : "Switch to dark mode"}
          >
            {theme === "dark" ? (
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"><circle cx="8" cy="8" r="3" /><path d="M8 1.5v1M8 13.5v1M1.5 8h1M13.5 8h1M3.4 3.4l.7.7M11.9 11.9l.7.7M3.4 12.6l.7-.7M11.9 4.1l.7-.7" /></svg>
            ) : (
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"><path d="M13.5 8.5a5.5 5.5 0 0 1-7-7 5.5 5.5 0 1 0 7 7z" /></svg>
            )}
          </button>
          <span className="text-gray-500 dark:text-gray-400 text-sm">{user?.email}</span>
          <button
            onClick={logout}
            className="text-sm text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white transition-colors cursor-pointer"
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
