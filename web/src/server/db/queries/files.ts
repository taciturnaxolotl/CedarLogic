import { Database } from "bun:sqlite";
import type { FileRecord, FilePermission, PermissionLevel, FileWithPermission } from "@shared/types";

export function createFile(db: Database, ownerId: string, title?: string): FileRecord {
  const stmt = title
    ? db.query("INSERT INTO files (owner_id, title) VALUES (?, ?) RETURNING *")
    : db.query("INSERT INTO files (owner_id) VALUES (?) RETURNING *");
  const row = (title ? stmt.get(ownerId, title) : stmt.get(ownerId)) as any;
  return mapFile(row);
}

export function getFile(db: Database, fileId: string): FileRecord | null {
  const row = db.query("SELECT * FROM files WHERE id = ?").get(fileId) as any;
  return row ? mapFile(row) : null;
}

export function listUserFiles(db: Database, userId: string, userEmail: string): FileWithPermission[] {
  const rows = db.query(`
    SELECT DISTINCT f.*, u.name as owner_name,
      CASE
        WHEN f.owner_id = ? THEN 'owner'
        WHEN fp.role IS NOT NULL THEN fp.role
        ELSE NULL
      END as permission
    FROM files f
    LEFT JOIN users u ON f.owner_id = u.id
    LEFT JOIN file_permissions fp ON fp.file_id = f.id AND (fp.user_id = ? OR fp.user_email = ?)
    WHERE f.owner_id = ? OR fp.user_id IS NOT NULL OR fp.user_email IS NOT NULL
    ORDER BY f.updated_at DESC
  `).all(userId, userId, userEmail, userId) as any[];
  return rows.map((r: any) => ({
    ...mapFile(r),
    permission: r.permission as PermissionLevel,
    ownerName: r.owner_name,
  }));
}

export function updateFile(
  db: Database,
  fileId: string,
  data: { title?: string; linkSharing?: string }
): FileRecord | null {
  const sets: string[] = [];
  const values: any[] = [];
  if (data.title !== undefined) {
    sets.push("title = ?");
    values.push(data.title);
  }
  if (data.linkSharing !== undefined) {
    sets.push("link_sharing = ?");
    values.push(data.linkSharing);
  }
  if (sets.length === 0) return getFile(db, fileId);
  sets.push("updated_at = datetime('now')");
  values.push(fileId);
  db.query(`UPDATE files SET ${sets.join(", ")} WHERE id = ?`).run(...values);
  return getFile(db, fileId);
}

export function deleteFile(db: Database, fileId: string): void {
  db.query("DELETE FROM files WHERE id = ?").run(fileId);
}

export function resolvePermission(
  db: Database,
  fileId: string,
  userId: string | null,
  userEmail: string | null
): PermissionLevel {
  const file = db.query("SELECT * FROM files WHERE id = ?").get(fileId) as any;
  if (!file) return null;

  if (userId && file.owner_id === userId) return "owner";

  // Check explicit permission
  if (userId || userEmail) {
    const perm = db.query(`
      SELECT role FROM file_permissions
      WHERE file_id = ? AND (user_id = ? OR user_email = ?)
      LIMIT 1
    `).get(fileId, userId, userEmail) as any;
    if (perm) return perm.role as PermissionLevel;
  }

  // Check link sharing
  if (file.link_sharing !== "private") {
    return file.link_sharing as PermissionLevel;
  }

  return null;
}

export function addPermission(
  db: Database,
  fileId: string,
  userEmail: string,
  role: "viewer" | "editor"
): FilePermission {
  // Try to resolve user_id from email
  const user = db.query("SELECT id FROM users WHERE email = ?").get(userEmail) as any;
  const userId = user?.id ?? null;

  const row = db.query(`
    INSERT INTO file_permissions (file_id, user_email, user_id, role)
    VALUES (?, ?, ?, ?)
    ON CONFLICT(file_id, user_email) DO UPDATE SET role = excluded.role
    RETURNING *
  `).get(fileId, userEmail, userId, role) as any;

  return mapPermission(row);
}

export function removePermission(db: Database, permId: string): void {
  db.query("DELETE FROM file_permissions WHERE id = ?").run(permId);
}

export function getFilePermissions(db: Database, fileId: string): FilePermission[] {
  const rows = db.query("SELECT * FROM file_permissions WHERE file_id = ?").all(fileId) as any[];
  return rows.map(mapPermission);
}

export function claimPendingPermissions(db: Database, userId: string, email: string): void {
  db.query("UPDATE file_permissions SET user_id = ? WHERE user_email = ? AND user_id IS NULL").run(
    userId,
    email
  );
}

// Yjs document persistence
export function loadYjsState(db: Database, fileId: string): Buffer | null {
  const row = db.query("SELECT state FROM yjs_documents WHERE file_id = ?").get(fileId) as any;
  return row ? row.state : null;
}

export function saveYjsState(db: Database, fileId: string, state: Uint8Array): void {
  db.query(`
    INSERT INTO yjs_documents (file_id, state, updated_at)
    VALUES (?, ?, datetime('now'))
    ON CONFLICT(file_id) DO UPDATE SET state = excluded.state, updated_at = datetime('now')
  `).run(fileId, state);
}

function mapFile(row: any): FileRecord {
  return {
    id: row.id,
    ownerId: row.owner_id,
    title: row.title,
    linkSharing: row.link_sharing,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}

function mapPermission(row: any): FilePermission {
  return {
    id: row.id,
    fileId: row.file_id,
    userEmail: row.user_email,
    userId: row.user_id,
    role: row.role,
    createdAt: row.created_at,
  };
}
