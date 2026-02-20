import { CircuitCard } from "./CircuitCard";
import type { FileWithPermission, ThumbnailData } from "@shared/types";

interface CircuitCardGridProps {
  files: FileWithPermission[];
  thumbnails: Record<string, ThumbnailData>;
  emptyMessage: string;
  onOpenFile: (fileId: string) => void;
  onDeleteFile: (fileId: string, e: React.MouseEvent) => void;
}

export function CircuitCardGrid({
  files,
  thumbnails,
  emptyMessage,
  onOpenFile,
  onDeleteFile,
}: CircuitCardGridProps) {
  if (files.length === 0) {
    return (
      <p className="text-gray-500 text-sm py-8 text-center">{emptyMessage}</p>
    );
  }

  return (
    <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
      {files.map((file) => (
        <CircuitCard
          key={file.id}
          file={file}
          thumbnail={thumbnails[file.id]}
          onClick={() => onOpenFile(file.id)}
          onDelete={(e) => onDeleteFile(file.id, e)}
        />
      ))}
    </div>
  );
}
