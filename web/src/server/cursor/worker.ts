// Bun Worker: dedicated cursor relay server on port 3002
// Runs in a separate thread to isolate high-frequency cursor I/O

import { verifyToken } from "../middleware/auth";
import { resolvePermission } from "../db/queries/files";
import { initDb } from "../db/index";
import {
  CURSOR_MOVE,
  CURSOR_LEAVE,
  VIEWPORT_UPDATE,
  encodeCursorLeave,
} from "./protocol";

interface ClientState {
  ws: any; // Bun ServerWebSocket
  userHash: number;
  roomId: string;
  // Viewport bounds for culling
  viewMinX: number;
  viewMinY: number;
  viewMaxX: number;
  viewMaxY: number;
  viewportSet: boolean;
}

const rooms = new Map<string, Set<ClientState>>();
const pending = new Map<any, { roomId: string; token: string } | null>();
const db = initDb();

const VIEWPORT_PADDING = 0.2; // 20% padding for culling

function isInViewport(
  client: ClientState,
  x: number,
  y: number,
): boolean {
  if (!client.viewportSet) return true; // no viewport reported yet, always relay
  const padX = (client.viewMaxX - client.viewMinX) * VIEWPORT_PADDING;
  const padY = (client.viewMaxY - client.viewMinY) * VIEWPORT_PADDING;
  return (
    x >= client.viewMinX - padX &&
    x <= client.viewMaxX + padX &&
    y >= client.viewMinY - padY &&
    y <= client.viewMaxY + padY
  );
}

function removeClient(client: ClientState) {
  const room = rooms.get(client.roomId);
  if (!room) return;
  room.delete(client);

  // Broadcast CURSOR_LEAVE to remaining peers
  const leaveMsg = encodeCursorLeave(client.userHash);
  for (const peer of room) {
    peer.ws.send(leaveMsg);
  }

  if (room.size === 0) rooms.delete(client.roomId);
}

const server = Bun.serve({
  port: 3002,
  fetch(req, server) {
    if (server.upgrade(req)) return undefined;
    return new Response("Cursor relay", { status: 200 });
  },
  websocket: {
    open(ws) {
      // Mark as pending auth — first message must be AUTH
      pending.set(ws, null);
    },

    async message(ws, data) {
      // Handle AUTH (first message, text)
      if (pending.has(ws)) {
        if (typeof data !== "string") {
          ws.close(4001, "Expected auth message");
          return;
        }

        const colonIdx = data.indexOf(":");
        if (colonIdx === -1) {
          ws.close(4001, "Invalid auth format");
          return;
        }

        const roomId = data.slice(0, colonIdx);
        const token = data.slice(colonIdx + 1);

        // Handle anonymous/public access
        let userHash = 0;
        if (token.startsWith("public:")) {
          const perm = resolvePermission(db, roomId, null, null);
          if (!perm) {
            ws.close(4003, "Access denied");
            return;
          }
          // Anonymous user gets a random hash
          userHash = (Math.random() * 0xffffffff) >>> 0;
        } else {
          const payload = await verifyToken(token);
          if (!payload?.sub) {
            ws.close(4003, "Invalid token");
            return;
          }
          // Verify file access
          const perm = resolvePermission(
            db,
            roomId,
            payload.sub,
            (payload.email as string) || null,
          );
          if (!perm) {
            ws.close(4003, "Access denied");
            return;
          }
          // Use FNV hash of userId
          userHash = hashUserIdFnv(payload.sub);
        }

        pending.delete(ws);

        const client: ClientState = {
          ws,
          userHash,
          roomId,
          viewMinX: 0,
          viewMinY: 0,
          viewMaxX: 0,
          viewMaxY: 0,
          viewportSet: false,
        };

        // Store client ref on ws for close handler
        (ws as any)._client = client;

        if (!rooms.has(roomId)) rooms.set(roomId, new Set());
        rooms.get(roomId)!.add(client);
        return;
      }

      // Binary messages after auth
      if (typeof data === "string") return;

      const client: ClientState | undefined = (ws as any)._client;
      if (!client) return;

      const buf = data instanceof ArrayBuffer ? data : data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
      if (buf.byteLength < 1) return;
      const view = new DataView(buf);
      const type = view.getUint8(0);

      switch (type) {
        case CURSOR_MOVE: {
          if (buf.byteLength < 13) return;
          // Fast path: parse x,y for viewport culling, relay raw bytes
          const x = view.getFloat32(5, true);
          const y = view.getFloat32(9, true);
          const room = rooms.get(client.roomId);
          if (!room) return;
          for (const peer of room) {
            if (peer === client) continue;
            if (!isInViewport(peer, x, y)) continue;
            peer.ws.send(buf);
          }
          break;
        }

        case CURSOR_LEAVE: {
          if (buf.byteLength < 5) return;
          const room = rooms.get(client.roomId);
          if (!room) return;
          for (const peer of room) {
            if (peer === client) continue;
            peer.ws.send(buf);
          }
          break;
        }

        case VIEWPORT_UPDATE: {
          if (buf.byteLength < 17) return;
          client.viewMinX = view.getFloat32(1, true);
          client.viewMinY = view.getFloat32(5, true);
          client.viewMaxX = view.getFloat32(9, true);
          client.viewMaxY = view.getFloat32(13, true);
          client.viewportSet = true;
          break;
        }
      }
    },

    close(ws) {
      pending.delete(ws);
      const client: ClientState | undefined = (ws as any)._client;
      if (client) removeClient(client);
    },
  },
});

console.log(`Cursor relay running on ws://localhost:${server.port}`);

// FNV-1a hash (matches protocol.ts hashUserId)
function hashUserIdFnv(userId: string): number {
  let h = 0x811c9dc5;
  for (let i = 0; i < userId.length; i++) {
    h ^= userId.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}
