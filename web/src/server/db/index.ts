import { Database } from "bun:sqlite";
import { readFileSync, readdirSync } from "fs";
import { resolve } from "path";

const DB_PATH = process.env.DATABASE_PATH || resolve(import.meta.dirname!, "../../../data/cedarlogic.db");
const MIGRATIONS_DIR = resolve(import.meta.dirname!, "migrations");

export function initDb(): Database {
  const db = new Database(DB_PATH, { create: true });
  db.exec("PRAGMA journal_mode = WAL");
  db.exec("PRAGMA foreign_keys = ON");

  // Run migrations
  db.exec(`
    CREATE TABLE IF NOT EXISTS _migrations (
      name TEXT PRIMARY KEY,
      applied_at TEXT NOT NULL DEFAULT (datetime('now'))
    )
  `);

  const applied = new Set(
    db
      .query("SELECT name FROM _migrations")
      .all()
      .map((r: any) => r.name)
  );

  const files = readdirSync(MIGRATIONS_DIR)
    .filter((f) => f.endsWith(".sql"))
    .sort();

  for (const file of files) {
    if (applied.has(file)) continue;
    const sql = readFileSync(resolve(MIGRATIONS_DIR, file), "utf-8");
    db.exec(sql);
    db.query("INSERT INTO _migrations (name) VALUES (?)").run(file);
    console.log(`Applied migration: ${file}`);
  }

  return db;
}
