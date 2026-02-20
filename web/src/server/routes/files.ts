import { Database } from "bun:sqlite";
import * as Y from "yjs";
import { authenticateRequest } from "../middleware/auth";
import {
  createFile,
  getFile,
  listUserFiles,
  updateFile,
  deleteFile,
  resolvePermission,
  addPermission,
  removePermission,
  getFilePermissions,
  loadYjsState,
} from "../db/queries/files";
import type { ThumbnailData } from "@shared/types";

export async function fileRoutes(req: Request, db: Database): Promise<Response> {
  const url = new URL(req.url);
  const auth = await authenticateRequest(req);

  // POST /api/files — create
  if (url.pathname === "/api/files" && req.method === "POST") {
    if (!auth) return Response.json({ error: "Unauthorized" }, { status: 401 });
    const body = (await req.json().catch(() => ({}))) as { title?: string };
    const file = createFile(db, auth.userId, body.title);
    return Response.json(file, { status: 201 });
  }

  // GET /api/files — list user's files
  if (url.pathname === "/api/files" && req.method === "GET") {
    if (!auth) return Response.json({ error: "Unauthorized" }, { status: 401 });
    const files = listUserFiles(db, auth.userId, auth.email);
    return Response.json(files);
  }

  // POST /api/files/thumbnails — get thumbnail data for multiple files
  if (url.pathname === "/api/files/thumbnails" && req.method === "POST") {
    if (!auth) return Response.json({ error: "Unauthorized" }, { status: 401 });
    const body = (await req.json().catch(() => ({}))) as { fileIds?: string[] };
    const fileIds = body.fileIds;
    if (!Array.isArray(fileIds)) {
      return Response.json({ error: "fileIds array required" }, { status: 400 });
    }

    // Only process files the user has access to
    const userFiles = listUserFiles(db, auth.userId, auth.email);
    const accessibleIds = new Set(userFiles.map((f) => f.id));

    const result: Record<string, ThumbnailData> = {};
    for (const fileId of fileIds) {
      if (!accessibleIds.has(fileId)) continue;
      const state = loadYjsState(db, fileId);
      if (!state) {
        result[fileId] = { gates: [], wires: [] };
        continue;
      }

      const doc = new Y.Doc();
      Y.applyUpdate(doc, new Uint8Array(state));

      const gates: ThumbnailData["gates"] = [];
      const gatesMap = doc.getMap("gates") as Y.Map<Y.Map<any>>;
      gatesMap.forEach((yGate) => {
        gates.push({
          defId: yGate.get("defId"),
          x: yGate.get("x"),
          y: yGate.get("y"),
          rotation: yGate.get("rotation") ?? 0,
        });
      });

      const wires: ThumbnailData["wires"] = [];
      const wiresMap = doc.getMap("wires") as Y.Map<Y.Map<any>>;
      wiresMap.forEach((yWire) => {
        const segments: Array<{ x1: number; y1: number; x2: number; y2: number }> = [];

        // Try new model format first
        const modelStr = yWire.get("model");
        if (modelStr) {
          try {
            const model = JSON.parse(modelStr);
            for (const seg of Object.values(model.segMap) as any[]) {
              segments.push({
                x1: seg.begin.x,
                y1: seg.begin.y,
                x2: seg.end.x,
                y2: seg.end.y,
              });
            }
          } catch {}
        } else {
          // Legacy format
          const segStr = yWire.get("segments");
          if (segStr) {
            try {
              const segs = JSON.parse(segStr);
              segments.push(...segs);
            } catch {}
          }
        }

        if (segments.length > 0) {
          wires.push({ segments });
        }
      });

      result[fileId] = { gates, wires };
      doc.destroy();
    }

    return Response.json(result);
  }

  // Match /api/files/:id
  const fileMatch = url.pathname.match(/^\/api\/files\/([^/]+)$/);
  if (fileMatch) {
    const fileId = fileMatch[1];

    // GET /api/files/:id
    if (req.method === "GET") {
      const file = getFile(db, fileId);
      if (!file) return Response.json({ error: "Not found" }, { status: 404 });

      const permission = resolvePermission(
        db,
        fileId,
        auth?.userId ?? null,
        auth?.email ?? null
      );
      if (!permission) return Response.json({ error: "Forbidden" }, { status: 403 });

      const permissions = permission === "owner" ? getFilePermissions(db, fileId) : [];
      return Response.json({ ...file, permission, permissions });
    }

    // PATCH /api/files/:id
    if (req.method === "PATCH") {
      if (!auth) return Response.json({ error: "Unauthorized" }, { status: 401 });
      const perm = resolvePermission(db, fileId, auth.userId, auth.email);
      if (perm !== "owner") return Response.json({ error: "Forbidden" }, { status: 403 });

      const body = (await req.json()) as { title?: string; linkSharing?: string };
      const updated = updateFile(db, fileId, body);
      return Response.json(updated);
    }

    // DELETE /api/files/:id
    if (req.method === "DELETE") {
      if (!auth) return Response.json({ error: "Unauthorized" }, { status: 401 });
      const perm = resolvePermission(db, fileId, auth.userId, auth.email);
      if (perm !== "owner") return Response.json({ error: "Forbidden" }, { status: 403 });

      deleteFile(db, fileId);
      return new Response(null, { status: 204 });
    }
  }

  // POST /api/files/:id/permissions
  const permAddMatch = url.pathname.match(/^\/api\/files\/([^/]+)\/permissions$/);
  if (permAddMatch && req.method === "POST") {
    if (!auth) return Response.json({ error: "Unauthorized" }, { status: 401 });
    const fileId = permAddMatch[1];
    const perm = resolvePermission(db, fileId, auth.userId, auth.email);
    if (perm !== "owner") return Response.json({ error: "Forbidden" }, { status: 403 });

    const body = (await req.json()) as { email: string; role: "viewer" | "editor" };
    if (!body.email || !body.role) {
      return Response.json({ error: "email and role required" }, { status: 400 });
    }
    const permission = addPermission(db, fileId, body.email, body.role);
    return Response.json(permission, { status: 201 });
  }

  // DELETE /api/files/:id/permissions/:permId
  const permDelMatch = url.pathname.match(
    /^\/api\/files\/([^/]+)\/permissions\/([^/]+)$/
  );
  if (permDelMatch && req.method === "DELETE") {
    if (!auth) return Response.json({ error: "Unauthorized" }, { status: 401 });
    const fileId = permDelMatch[1];
    const perm = resolvePermission(db, fileId, auth.userId, auth.email);
    if (perm !== "owner") return Response.json({ error: "Forbidden" }, { status: 403 });

    removePermission(db, permDelMatch[2]);
    return new Response(null, { status: 204 });
  }

  return new Response("Not Found", { status: 404 });
}
