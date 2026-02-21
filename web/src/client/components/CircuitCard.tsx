import { CircuitThumbnail } from "./CircuitThumbnail";
import type { FileWithPermission, ThumbnailData } from "@shared/types";

interface CircuitCardProps {
  file: FileWithPermission;
  thumbnail: ThumbnailData | undefined;
  onClick: () => void;
  onDelete: (e: React.MouseEvent) => void;
}

function relativeDate(dateStr: string): string {
  const now = Date.now();
  const then = new Date(dateStr).getTime();
  const diff = now - then;
  const mins = Math.floor(diff / 60000);
  if (mins < 1) return "just now";
  if (mins < 60) return `${mins}m ago`;
  const hours = Math.floor(mins / 60);
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  if (days < 30) return `${days}d ago`;
  return new Date(dateStr).toLocaleDateString();
}

export function CircuitCard({ file, thumbnail, onClick, onDelete }: CircuitCardProps) {
  return (
    <div
      onClick={onClick}
      className="group relative bg-gray-50 dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 hover:border-gray-400 dark:hover:border-gray-600 cursor-pointer transition-all overflow-hidden flex flex-col hover:shadow-md hover:-translate-y-0.5"
    >
      {file.permission === "owner" && (
        <button
          onClick={(e) => {
            e.stopPropagation();
            onDelete(e);
          }}
          className="absolute top-2 right-2 z-10 p-1.5 text-gray-500 dark:text-gray-400 hover:text-red-400 opacity-0 group-hover:opacity-100 transition-opacity cursor-pointer"
        >
          <svg className="w-[18px] h-[18px]" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth={2.5}>
            <path strokeLinecap="round" strokeLinejoin="round" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
          </svg>
        </button>
      )}
      <CircuitThumbnail data={thumbnail} />
      <div className="p-3 flex flex-col gap-1">
        <div className="font-medium text-sm truncate">{file.title}</div>
        <div className="text-xs text-gray-400 dark:text-gray-500">
          {file.permission === "owner" ? "Owned by you" : `Shared by ${file.ownerName}`}
          {" · "}
          {relativeDate(file.updatedAt)}
        </div>
      </div>
    </div>
  );
}
