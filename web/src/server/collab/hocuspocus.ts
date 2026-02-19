import { Database } from "bun:sqlite";
import { Server } from "@hocuspocus/server";
import { verifyToken } from "../middleware/auth";
import { resolvePermission, loadYjsState, saveYjsState } from "../db/queries/files";
import * as Y from "yjs";

let saveTimeout: ReturnType<typeof setTimeout> | null = null;

export function setupHocuspocus(db: Database) {
  const server = Server.configure({
    async onAuthenticate({ token, documentName }: any) {
      const fileId = documentName;

      // Handle anonymous/public access via "public:<fileId>" token
      if (token?.startsWith("public:")) {
        const permission = resolvePermission(db, fileId, null, null);
        if (!permission) throw new Error("Access denied");

        return {
          user: {
            id: "anonymous",
            email: "anonymous",
            name: "Anonymous",
          },
          readOnly: permission === "viewer",
        };
      }

      // Authenticated user flow
      if (!token) throw new Error("No token provided");

      const payload = await verifyToken(token);
      if (!payload?.sub || !payload.email) throw new Error("Invalid token");

      const permission = resolvePermission(
        db,
        fileId,
        payload.sub,
        payload.email as string
      );

      if (!permission) throw new Error("Access denied");

      return {
        user: {
          id: payload.sub,
          email: payload.email as string,
          name: payload.email as string,
        },
        readOnly: permission === "viewer",
      };
    },

    async onLoadDocument({ document, documentName }: any) {
      const state = loadYjsState(db, documentName);
      if (state) {
        Y.applyUpdate(document, new Uint8Array(state));
      }
      return document;
    },

    async onStoreDocument({ document, documentName }: any) {
      if (saveTimeout) clearTimeout(saveTimeout);
      saveTimeout = setTimeout(() => {
        const state = Y.encodeStateAsUpdate(document);
        saveYjsState(db, documentName, state);
      }, 2000);
    },
  });

  return {
    server,
    // Hocuspocus listens on its own port for WebSocket connections.
    // We start it and let the client connect directly.
    async listen(port: number = 3001) {
      await server.listen(port);
      console.log(`Hocuspocus WebSocket server running on ws://localhost:${port}`);
    },
  };
}
