import { Database } from "bun:sqlite";
import type { User } from "@shared/types";

export function upsertUser(
  db: Database,
  data: { googleId: string; email: string; name: string; avatarUrl: string | null }
): User {
  const existing = db
    .query("SELECT * FROM users WHERE google_id = ?")
    .get(data.googleId) as any;

  if (existing) {
    db.query(
      "UPDATE users SET email = ?, name = ?, avatar_url = ?, updated_at = datetime('now') WHERE id = ?"
    ).run(data.email, data.name, data.avatarUrl, existing.id);
    return mapUser(
      db.query("SELECT * FROM users WHERE id = ?").get(existing.id) as any
    );
  }

  db.query(
    "INSERT INTO users (google_id, email, name, avatar_url) VALUES (?, ?, ?, ?)"
  ).run(data.googleId, data.email, data.name, data.avatarUrl);

  return mapUser(
    db.query("SELECT * FROM users WHERE google_id = ?").get(data.googleId) as any
  );
}

export function findUserById(db: Database, id: string): User | null {
  const row = db.query("SELECT * FROM users WHERE id = ?").get(id) as any;
  return row ? mapUser(row) : null;
}

export function findUserByEmail(db: Database, email: string): User | null {
  const row = db.query("SELECT * FROM users WHERE email = ?").get(email) as any;
  return row ? mapUser(row) : null;
}

function mapUser(row: any): User {
  return {
    id: row.id,
    googleId: row.google_id,
    email: row.email,
    name: row.name,
    avatarUrl: row.avatar_url,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}
