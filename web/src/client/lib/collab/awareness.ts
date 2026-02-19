import type { HocuspocusProvider } from "@hocuspocus/provider";

export interface CursorState {
  x: number;
  y: number;
  user: { name: string; color: string; avatarUrl?: string | null };
  selection?: string[];
}

const CURSOR_COLORS = [
  "#FF6B6B",
  "#4ECDC4",
  "#45B7D1",
  "#96CEB4",
  "#FFEAA7",
  "#DDA0DD",
  "#98D8C8",
  "#F7DC6F",
];

const ANIMALS = ["Fox", "Owl", "Bear", "Wolf", "Hawk", "Lynx", "Deer", "Hare"];
const ADJECTIVES = ["Red", "Blue", "Gold", "Gray", "Jade", "Sage", "Teal", "Plum"];

let colorIndex = 0;

export function generateAnonName(): string {
  const adj = ADJECTIVES[Math.floor(Math.random() * ADJECTIVES.length)];
  const animal = ANIMALS[Math.floor(Math.random() * ANIMALS.length)];
  return `${adj} ${animal}`;
}

export function setupAwareness(
  provider: HocuspocusProvider,
  userName: string,
  avatarUrl?: string | null,
  role?: string | null,
) {
  const color = CURSOR_COLORS[colorIndex++ % CURSOR_COLORS.length];
  const awareness = provider.awareness!;

  let lastUpdate = 0;

  awareness.setLocalStateField("user", { name: userName, color, avatarUrl: avatarUrl ?? null, role: role ?? "viewer" });

  function updateCursor(x: number, y: number, selection?: string[]) {
    const now = Date.now();
    if (now - lastUpdate < 100) return; // Throttle to 100ms
    lastUpdate = now;

    awareness.setLocalStateField("cursor", {
      x,
      y,
      user: { name: userName, color, avatarUrl: avatarUrl ?? null },
      selection,
    });
  }

  function clearCursor() {
    awareness.setLocalStateField("cursor", null);
  }

  function getRemoteCursors(): Map<number, CursorState> {
    const cursors = new Map<number, CursorState>();
    awareness.getStates().forEach((state, clientId) => {
      if (clientId !== awareness.clientID && state.cursor) {
        cursors.set(clientId, state.cursor as CursorState);
      }
    });
    return cursors;
  }

  return { updateCursor, clearCursor, getRemoteCursors, awareness };
}
