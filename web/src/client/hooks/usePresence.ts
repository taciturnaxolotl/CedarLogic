import { useEffect, useState, useRef, useMemo, useCallback } from "react";
import type { HocuspocusProvider } from "@hocuspocus/provider";
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
  userId: string,
  role?: string | null,
) {
  const [remoteUsers, setRemoteUsers] = useState<RemoteUser[]>([]);
  const [userMetaByHash, setUserMetaByHash] = useState<Map<number, { name: string; color: string }>>(new Map());
  const [userPageByHash, setUserPageByHash] = useState<Map<number, string>>(new Map());
  const setActivePageRef = useRef<((page: string) => void) | null>(null);
  const anonName = useMemo(() => generateAnonName(), []);

  const userName = user?.name ?? anonName;
  const avatarUrl = user?.avatarUrl ?? null;

  const setActivePageInAwareness = useCallback((page: string) => {
    setActivePageRef.current?.(page);
  }, []);

  useEffect(() => {
    if (!provider) return;

    const result = setupAwareness(provider, userName, userId, avatarUrl, role);
    const aw = result.awareness;
    setActivePageRef.current = result.setActivePage;
    let prevUserKey = "";

    function syncUsers() {
      const users: RemoteUser[] = [];
      const meta = new Map<number, { name: string; color: string }>();
      const pages = new Map<number, string>();
      const seen = new Set<string>();

      aw.getStates().forEach((state, clientId) => {
        if (clientId === aw.clientID) return;
        const u = state.user;
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
        if (u.userHash != null) {
          meta.set(u.userHash, { name: u.name, color: u.color });
          pages.set(u.userHash, u.activePage ?? "0");
        }
      });

      const key = users.map((u) => `${u.name}:${u.role}`).sort().join(",");
      if (key !== prevUserKey) {
        prevUserKey = key;
        setRemoteUsers(users);
      }
      setUserMetaByHash(meta);
      setUserPageByHash(pages);
    }

    aw.on("change", syncUsers);
    syncUsers();

    return () => {
      aw.off("change", syncUsers);
      setActivePageRef.current = null;
    };
  }, [provider, userName, userId, avatarUrl, role]);

  return { remoteUsers, userMetaByHash, userPageByHash, setActivePageInAwareness };
}
