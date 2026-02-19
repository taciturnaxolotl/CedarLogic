import { initDb } from "./db/index";
import { authRoutes } from "./routes/auth";
import { fileRoutes } from "./routes/files";
import { setupHocuspocus } from "./collab/hocuspocus";

const db = initDb();

// Start Hocuspocus WebSocket server on port 3001
const hocuspocus = setupHocuspocus(db);
hocuspocus.listen(3001);

// HTTP API server on port 3000
const server = Bun.serve({
  port: 3000,
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
