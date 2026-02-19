import { useState, useEffect, useCallback } from "react";
import type { PublicUser } from "@shared/types";

interface AuthState {
  user: PublicUser | null;
  loading: boolean;
  login: () => void;
  logout: () => Promise<void>;
}

export function useAuth(): AuthState {
  const [user, setUser] = useState<PublicUser | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchUser();
  }, []);

  async function fetchUser() {
    try {
      const res = await fetch("/api/auth/me");
      if (res.ok) {
        const data = await res.json();
        setUser(data.user);
      }
    } catch {
      // Not logged in
    } finally {
      setLoading(false);
    }
  }

  const login = useCallback(() => {
    window.location.href = "/auth/google";
  }, []);

  const logout = useCallback(async () => {
    await fetch("/auth/logout", { method: "POST" });
    setUser(null);
  }, []);

  return { user, loading, login, logout };
}
