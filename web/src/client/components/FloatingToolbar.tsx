import { useState, useRef, useCallback } from "react";
import { useThemeStore } from "../stores/theme-store";
import { useSimulationStore } from "../stores/simulation-store";
import { usePanelStore } from "../stores/panel-store";
import { GatePalette } from "./Toolbar";
import type { PublicUser, PermissionLevel, FileRecord } from "@shared/types";

const MIN_HZ = 1;
const MAX_HZ = 100;
const LOG_MIN = Math.log(MIN_HZ);
const LOG_MAX = Math.log(MAX_HZ);
function sliderToHz(t: number): number {
  return Math.round(Math.exp(LOG_MIN + t * (LOG_MAX - LOG_MIN)));
}
function hzToSlider(hz: number): number {
  return (Math.log(hz) - LOG_MIN) / (LOG_MAX - LOG_MIN);
}

interface RemoteUser {
  clientId: number;
  name: string;
  color: string;
  avatarUrl?: string | null;
  role?: string | null;
}

interface FloatingToolbarProps {
  file: FileRecord & { permission: PermissionLevel };
  onBack: (() => void) | null;
  onTitleSave: (title: string) => void;
  connected: boolean;
  readOnly: boolean;
  isOwner: boolean;
  currentUser?: PublicUser | null;
  permission: PermissionLevel | null;
  remoteUsers: RemoteUser[];
  onShareOpen: () => void;
  onImportClick: () => void;
  onExport: () => void;
}

export function FloatingToolbar({
  file,
  onBack,
  onTitleSave,
  connected,
  readOnly,
  isOwner,
  currentUser,
  permission,
  remoteUsers,
  onShareOpen,
  onImportClick,
  onExport,
}: FloatingToolbarProps) {
  const { theme, toggle: toggleTheme } = useThemeStore();
  const [editingTitle, setEditingTitle] = useState(false);
  const [presenceOpen, setPresenceOpen] = useState(false);
  const titleInputRef = useRef<HTMLInputElement>(null);

  const leftOpen = usePanelStore((s) => s.leftOpen);
  const toggleLeft = usePanelStore((s) => s.toggleLeft);

  const running = useSimulationStore((s) => s.running);
  const setRunning = useSimulationStore((s) => s.setRunning);
  const stepsPerFrame = useSimulationStore((s) => s.stepsPerFrame);
  const setStepsPerFrame = useSimulationStore((s) => s.setStepsPerFrame);

  const saveTitle = useCallback(
    (newTitle: string) => {
      const trimmed = newTitle.trim();
      if (trimmed && trimmed !== file.title) {
        onTitleSave(trimmed);
      }
      setEditingTitle(false);
    },
    [file.title, onTitleSave],
  );

  const barClass = "bg-gray-100/80 dark:bg-gray-900/80 backdrop-blur-sm border border-gray-200 dark:border-gray-800 rounded-xl px-2 py-1 shadow-lg";

  /* ─── Shared left-bar content (sim controls only — title is centered separately) ─── */
  const leftBarContent = (
    <>
      {/* CedarLogic icon / back */}
      {onBack && (
        <>
          <button
            onClick={onBack}
            className="shrink-0 w-6 h-6 flex items-center justify-center rounded-full cursor-pointer hover:opacity-80 transition-opacity overflow-hidden"
            title="Back to files"
          >
            <img src="/icon.png" alt="CedarLogic" width={20} height={20} className="rounded-full" />
          </button>
          <div className="w-px h-4 bg-gray-300 dark:bg-gray-700" />
        </>
      )}

      {/* Sim controls */}
      <div className="flex items-center gap-1.5 flex-1 min-w-0">
        <button
          onClick={() => setRunning(!running)}
          className={`shrink-0 w-6 h-6 flex items-center justify-center rounded transition-colors cursor-pointer ${
            running
              ? "text-green-500 hover:bg-green-100 dark:hover:bg-green-900/30"
              : "text-yellow-500 hover:bg-yellow-100 dark:hover:bg-yellow-900/30"
          }`}
          title={running ? "Pause" : "Play"}
        >
          {running ? (
            <svg width="12" height="12" viewBox="0 0 14 14" fill="currentColor">
              <rect x="2" y="1" width="3.5" height="12" rx="0.5" />
              <rect x="8.5" y="1" width="3.5" height="12" rx="0.5" />
            </svg>
          ) : (
            <svg width="12" height="12" viewBox="0 0 14 14" fill="currentColor">
              <path d="M3 1.5v11l9-5.5z" />
            </svg>
          )}
        </button>
        <input
          type="range"
          min={0}
          max={1}
          step={0.001}
          value={hzToSlider(stepsPerFrame)}
          onChange={(e) => setStepsPerFrame(sliderToHz(Number(e.target.value)))}
          className="flex-1 min-w-0 h-1 accent-blue-500 cursor-pointer"
        />
        <span className="shrink-0 text-[11px] text-gray-400 dark:text-gray-500 tabular-nums">
          {stepsPerFrame} Hz
        </span>
      </div>
    </>
  );

  return (
    <>
      {/* ─── Left bar / morphing panel ─── */}
      <div className="absolute top-0 left-0 bottom-0 w-64 z-30 pointer-events-none">
        {/* Morphing background — content-independent */}
        <div
          className="absolute bg-gray-100/80 dark:bg-gray-900/80 backdrop-blur-sm border-gray-200 dark:border-gray-800 shadow-lg transition-all duration-200 ease-out"
          style={{
            top: leftOpen ? 0 : 12,
            left: leftOpen ? 0 : 12,
            right: leftOpen ? 0 : 12,
            bottom: leftOpen ? 0 : "calc(100% - 52px)",
            borderRadius: leftOpen ? 0 : 12,
            borderWidth: leftOpen ? "0 1px 0 0" : 1,
          }}
        />

        {/* Toolbar row — fixed position, never moves */}
        <div className="absolute top-[16px] left-[20px] right-[20px] flex items-center gap-1.5 pointer-events-auto">
          {leftBarContent}

          {/* Gate palette toggle — far right */}
          {!readOnly && (
            <>
              <div className="w-px h-4 bg-gray-300 dark:bg-gray-700" />
              <button
                onClick={toggleLeft}
                className={`shrink-0 w-7 h-7 flex items-center justify-center rounded-lg transition-colors cursor-pointer ${
                  leftOpen
                    ? "bg-blue-100 dark:bg-blue-900/50 text-blue-600 dark:text-blue-400"
                    : "text-gray-500 dark:text-gray-400 hover:bg-gray-200 dark:hover:bg-gray-800 hover:text-gray-900 dark:hover:text-white"
                }`}
                title={leftOpen ? "Close palette" : "Gate palette"}
              >
                <svg width="16" height="16" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
                  <rect x="2" y="4" width="14" height="10" rx="2" />
                  <path d="M2 9h5l2-2.5L11 11.5l2-2.5h3" />
                </svg>
              </button>
            </>
          )}
        </div>

        {/* Gate palette — below toolbar, clipped when closed */}
        <div
          className={`absolute top-[48px] left-0 right-0 bottom-0 overflow-hidden pointer-events-auto transition-opacity duration-200 ${
            leftOpen ? "opacity-100" : "opacity-0 pointer-events-none"
          }`}
        >
          <div className="border-t border-gray-200 dark:border-gray-800 h-full">
            <GatePalette />
          </div>
        </div>
      </div>

      {/* ─── Centered title (no background) ─── */}
      <div className="absolute top-3 left-1/2 -translate-x-1/2 z-20 pointer-events-auto flex items-center gap-2 py-1">
        {editingTitle ? (
          <input
            ref={titleInputRef}
            defaultValue={file.title}
            className="text-gray-900 dark:text-white font-medium text-sm bg-gray-200 dark:bg-gray-800 border border-gray-400 dark:border-gray-600 rounded px-2 py-0.5 outline-none focus:border-blue-500 w-44 text-center"
            autoFocus
            onBlur={(e) => saveTitle(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter") saveTitle(e.currentTarget.value);
              if (e.key === "Escape") setEditingTitle(false);
            }}
          />
        ) : (
          <span
            className={`text-gray-900 dark:text-white font-medium text-sm ${isOwner ? "cursor-pointer hover:text-blue-400 transition-colors" : ""}`}
            onClick={() => isOwner && setEditingTitle(true)}
          >
            {file.title}
          </span>
        )}
        {readOnly && (
          <span className="text-[10px] bg-gray-300 dark:bg-gray-700 text-gray-600 dark:text-gray-300 px-1.5 py-0.5 rounded">
            View only
          </span>
        )}
      </div>

      {/* ─── Right bar: actions ─── */}
      <div className={`absolute top-3 right-3 z-30 ${barClass} flex items-center gap-0.5`}>
        {!currentUser && (
          <a
            href="/auth/google"
            className="text-[11px] text-gray-600 dark:text-gray-300 bg-gray-200 dark:bg-gray-800 hover:bg-gray-300 dark:hover:bg-gray-700 px-2 py-0.5 rounded transition-colors mr-0.5"
          >
            Sign in
          </a>
        )}

        {!readOnly && (
          <button
            onClick={onImportClick}
            className="text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white transition-colors cursor-pointer p-1 rounded hover:bg-gray-200 dark:hover:bg-gray-700"
            title="Import .cdl file"
          >
            <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
              <path d="M8 10V2m0 0L5 5m3-3 3 3" />
              <path d="M2 10v2.5A1.5 1.5 0 0 0 3.5 14h9a1.5 1.5 0 0 0 1.5-1.5V10" />
            </svg>
          </button>
        )}
        <button
          onClick={onExport}
          className="text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white transition-colors cursor-pointer p-1 rounded hover:bg-gray-200 dark:hover:bg-gray-700"
          title="Export .cdl file"
        >
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
            <path d="M8 2v8m0 0 3-3M8 10 5 7" />
            <path d="M2 10v2.5A1.5 1.5 0 0 0 3.5 14h9a1.5 1.5 0 0 0 1.5-1.5V10" />
          </svg>
        </button>

        {isOwner && (
          <button
            onClick={onShareOpen}
            className="text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white transition-colors cursor-pointer p-1 rounded hover:bg-gray-200 dark:hover:bg-gray-700"
            title="Share"
          >
            <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
              <circle cx="12" cy="3.5" r="2" />
              <circle cx="12" cy="12.5" r="2" />
              <circle cx="4" cy="8" r="2" />
              <path d="m5.8 7 4.4-2.5M5.8 9l4.4 2.5" />
            </svg>
          </button>
        )}

        {/* Presence avatars */}
        {remoteUsers.length > 0 && (
          <>
            <div className="w-px h-4 bg-gray-300 dark:bg-gray-700 mx-0.5" />
            <div className="relative">
              <button
                onClick={() => setPresenceOpen(!presenceOpen)}
                className="flex items-center -space-x-1.5 cursor-pointer hover:opacity-80 transition-opacity"
              >
                {remoteUsers.slice(0, 5).map((u) => (
                  <div
                    key={u.clientId}
                    className="w-5 h-5 rounded-full border-2 border-gray-100 dark:border-gray-900 flex items-center justify-center text-[9px] font-medium shrink-0 overflow-hidden"
                    style={{ backgroundColor: u.avatarUrl ? undefined : u.color }}
                  >
                    {u.avatarUrl ? (
                      <img src={u.avatarUrl} alt={u.name} className="w-full h-full object-cover" referrerPolicy="no-referrer" />
                    ) : (
                      <span className="text-gray-900 dark:text-white">{u.name[0]}</span>
                    )}
                  </div>
                ))}
                {remoteUsers.length > 5 && (
                  <div className="w-5 h-5 rounded-full border-2 border-gray-100 dark:border-gray-900 bg-gray-300 dark:bg-gray-700 flex items-center justify-center text-[9px] font-medium text-gray-600 dark:text-gray-300 shrink-0">
                    +{remoteUsers.length - 5}
                  </div>
                )}
              </button>
              {presenceOpen && (
                <>
                  <div className="fixed inset-0 z-20" onClick={() => setPresenceOpen(false)} />
                  <div className="absolute right-0 top-full mt-2 z-30 bg-gray-200 dark:bg-gray-800 border border-gray-300 dark:border-gray-700 rounded-lg shadow-xl py-1 w-[240px]">
                    <div className="px-3 py-1.5 text-[11px] text-gray-400 dark:text-gray-500 font-medium uppercase tracking-wider">
                      Online — {remoteUsers.length + 1}
                    </div>
                    <div className="flex items-center gap-2.5 px-3 py-1.5">
                      <div
                        className="w-7 h-7 rounded-full flex items-center justify-center text-xs font-medium shrink-0 overflow-hidden"
                        style={{ backgroundColor: currentUser?.avatarUrl ? undefined : "#6B7280" }}
                      >
                        {currentUser?.avatarUrl ? (
                          <img src={currentUser.avatarUrl} alt={currentUser.name} className="w-full h-full object-cover" referrerPolicy="no-referrer" />
                        ) : (
                          <span className="text-gray-900 dark:text-white">{(currentUser?.name ?? "A")[0]}</span>
                        )}
                      </div>
                      <div className="min-w-0 flex-1">
                        <div className="text-sm text-gray-900 dark:text-white truncate">
                          {currentUser?.name ?? "Anonymous"} <span className="text-gray-400 dark:text-gray-500 text-xs">(you)</span>
                        </div>
                      </div>
                      <span className="text-[10px] px-1.5 py-0.5 rounded bg-gray-300 dark:bg-gray-700 text-gray-600 dark:text-gray-300 capitalize shrink-0">
                        {permission || "viewer"}
                      </span>
                    </div>
                    <div className="border-t border-gray-300 dark:border-gray-700 my-1" />
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
                              <span className="text-gray-900 dark:text-white">{u.name[0]}</span>
                            )}
                          </div>
                          <div className="min-w-0 flex-1">
                            <div className="text-sm text-gray-900 dark:text-white truncate">{u.name}</div>
                          </div>
                          <div className="flex items-center gap-1.5 shrink-0">
                            <span className="text-[10px] px-1.5 py-0.5 rounded bg-gray-300 dark:bg-gray-700 text-gray-600 dark:text-gray-300 capitalize">
                              {u.role || "viewer"}
                            </span>
                            <div className="w-2 h-2 rounded-full" style={{ backgroundColor: u.color }} />
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

        <div className="w-px h-4 bg-gray-300 dark:bg-gray-700 mx-0.5" />

        <button
          onClick={toggleTheme}
          className="text-gray-500 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white transition-colors cursor-pointer p-1 rounded hover:bg-gray-200 dark:hover:bg-gray-700"
          title={theme === "dark" ? "Switch to light mode" : "Switch to dark mode"}
        >
          {theme === "dark" ? (
            <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"><circle cx="8" cy="8" r="3" /><path d="M8 1.5v1M8 13.5v1M1.5 8h1M13.5 8h1M3.4 3.4l.7.7M11.9 11.9l.7.7M3.4 12.6l.7-.7M11.9 4.1l.7-.7" /></svg>
          ) : (
            <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"><path d="M13.5 8.5a5.5 5.5 0 0 1-7-7 5.5 5.5 0 1 0 7 7z" /></svg>
          )}
        </button>

        <span
          className={`w-2 h-2 rounded-full ${connected ? "bg-green-400" : "bg-red-400"}`}
          title={connected ? "Connected" : "Disconnected"}
        />
      </div>
    </>
  );
}
