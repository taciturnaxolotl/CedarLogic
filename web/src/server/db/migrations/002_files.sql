CREATE TABLE IF NOT EXISTS files (
  id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(16)))),
  owner_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  title TEXT NOT NULL DEFAULT 'Untitled Circuit',
  link_sharing TEXT NOT NULL DEFAULT 'private'
    CHECK (link_sharing IN ('private', 'viewer', 'editor')),
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS file_permissions (
  id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(16)))),
  file_id TEXT NOT NULL REFERENCES files(id) ON DELETE CASCADE,
  user_email TEXT NOT NULL,
  user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
  role TEXT NOT NULL CHECK (role IN ('viewer', 'editor')),
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(file_id, user_email)
);

CREATE TABLE IF NOT EXISTS yjs_documents (
  file_id TEXT PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
  state BLOB NOT NULL,
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
