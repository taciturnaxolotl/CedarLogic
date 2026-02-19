import { useEffect, useRef, useState } from "react";
import type { CollabProvider } from "../lib/collab/provider";
import { createCollabProvider } from "../lib/collab/provider";

export function useCollab(fileId: string | null) {
  const providerRef = useRef<CollabProvider | null>(null);
  const [connected, setConnected] = useState(false);
  const [synced, setSynced] = useState(false);

  useEffect(() => {
    if (!fileId) return;

    let cancelled = false;

    async function connect() {
      // Try to get a WS token (will fail for anonymous users)
      let token = "";
      try {
        const res = await fetch("/api/auth/ws-token", { method: "POST" });
        if (res.ok) {
          const data = await res.json();
          token = data.token;
        }
      } catch {
        // Anonymous user — connect without auth token
      }

      if (cancelled) return;

      // For anonymous users, use the fileId as a public token
      // The server will check link_sharing permissions
      const collab = createCollabProvider(fileId!, token || `public:${fileId}`);
      providerRef.current = collab;

      collab.provider.on("connect", () => setConnected(true));
      collab.provider.on("disconnect", () => setConnected(false));
      collab.provider.on("synced", () => setSynced(true));
    }

    connect();

    return () => {
      cancelled = true;
      providerRef.current?.destroy();
      providerRef.current = null;
      setConnected(false);
      setSynced(false);
    };
  }, [fileId]);

  return {
    doc: providerRef.current?.doc ?? null,
    provider: providerRef.current?.provider ?? null,
    connected,
    synced,
  };
}
