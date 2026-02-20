import { initDb } from "./db/index";
import { authRoutes } from "./routes/auth";
import { fileRoutes } from "./routes/files";
import { setupHocuspocus } from "./collab/hocuspocus";

const db = initDb();

const API_PORT = parseInt(process.env.PORT || "3000");
const WS_PORT = parseInt(process.env.WS_PORT || "3001");
const CURSOR_PORT = parseInt(process.env.CURSOR_PORT || "3002");

// Start Hocuspocus WebSocket server
const hocuspocus = setupHocuspocus(db);
hocuspocus.listen(WS_PORT);

// Start dedicated cursor relay worker
new Worker(new URL("./cursor/worker.ts", import.meta.url).href);

// HTTP API server
const server = Bun.serve({
  port: API_PORT,
  async fetch(req) {
    const url = new URL(req.url);

    // Health check
    if (url.pathname === "/api/health") {
      return Response.json({ status: "ok" });
    }

    // Auth routes
    if (url.pathname.startsWith("/auth/") || url.pathname.startsWith("/api/auth/")) {
      return authRoutes(req, db);
    }

    // File routes
    if (url.pathname.startsWith("/api/files")) {
      return fileRoutes(req, db);
    }

    return new Response("Not Found", { status: 404 });
  },
});

console.log(`API server running on http://localhost:${server.port}`);
