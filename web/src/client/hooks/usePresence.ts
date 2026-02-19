import { useEffect, useState, useRef, useMemo } from "react";
import type { HocuspocusProvider } from "@hocuspocus/provider";
import type { Awareness } from "y-protocols/awareness";
import {
  setupAwareness,
  generateAnonName,
} from "../lib/collab/awareness";

export interface RemoteUser {
  clientId: number;
  name: string;
  color: string;
  avatarUrl?: string | null;
  role?: string | null;
}

export function usePresence(
  provider: HocuspocusProvider | null,
  user: { name: string; avatarUrl?: string | null } | null,
  role?: string | null,
) {
  const [remoteUsers, setRemoteUsers] = useState<RemoteUser[]>([]);
  const [awareness, setAwareness] = useState<Awareness | null>(null);
  const awarenessRef = useRef<ReturnType<typeof setupAwareness> | null>(null);
  const anonName = useMemo(() => generateAnonName(), []);

  const userName = user?.name ?? anonName;
  const avatarUrl = user?.avatarUrl ?? null;

  useEffect(() => {
    if (!provider) return;

    const result = setupAwareness(provider, userName, avatarUrl, role);
    awarenessRef.current = result;
    setAwareness(result.awareness);

    const aw = result.awareness;
    let prevUserKey = "";

    function syncUsers() {
      const users: RemoteUser[] = [];
      const seen = new Set<string>();
      aw.getStates().forEach((state, clientId) => {
        if (clientId === aw.clientID) return;
        const u = state.user || state.cursor?.user;
        if (!u) return;
        if (seen.has(u.name)) return;
        seen.add(u.name);
        users.push({
          clientId,
          name: u.name,
          color: u.color,
          avatarUrl: u.avatarUrl ?? null,
          role: u.role ?? null,
        });
      });
      const key = users.map((u) => `${u.name}:${u.role}`).sort().join(",");
      if (key !== prevUserKey) {
        prevUserKey = key;
        setRemoteUsers(users);
      }
    }

    aw.on("change", syncUsers);
    syncUsers();

    return () => {
      aw.off("change", syncUsers);
      result.clearCursor();
      awarenessRef.current = null;
      setAwareness(null);
    };
  }, [provider, userName, avatarUrl, role]);

  const updateCursor = (x: number, y: number, selection?: string[]) => {
    awarenessRef.current?.updateCursor(x, y, selection);
  };

  const clearCursor = () => {
    awarenessRef.current?.clearCursor();
  };

  return { updateCursor, clearCursor, awareness, remoteUsers };
}
