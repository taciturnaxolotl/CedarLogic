import type { HocuspocusProvider } from "@hocuspocus/provider";

export interface CursorState {
  x: number;
  y: number;
  user: { name: string; color: string; avatarUrl?: string | null };
  selection?: string[];
}

const CURSOR_COLORS = [
  "#E06C75", // rose
  "#61AFEF", // blue
  "#98C379", // green
  "#C678DD", // purple
  "#E5C07B", // amber
  "#56B6C2", // cyan
  "#BE5046", // rust
  "#FF9640", // orange
  "#7EC8E3", // sky
  "#C991E1", // lavender
  "#6BCB77", // emerald
  "#FF6B81", // coral
];

const ANIMALS = ["Fox", "Owl", "Bear", "Wolf", "Hawk", "Lynx", "Deer", "Hare"];
const ADJECTIVES = ["Red", "Blue", "Gold", "Gray", "Jade", "Sage", "Teal", "Plum"];

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
  const awareness = provider.awareness!;

  let color = CURSOR_COLORS[awareness.clientID % CURSOR_COLORS.length];
  let lastUpdate = 0;
  let colorResolved = false;

  function pickUniqueColor() {
    if (colorResolved) return;
    const takenColors = new Set<string>();
    awareness.getStates().forEach((state, clientId) => {
      if (clientId === awareness.clientID) return;
      const c = state.user?.color;
      if (c) takenColors.add(c);
    });
    const unique = CURSOR_COLORS.find((c) => !takenColors.has(c));
    if (unique && unique !== color) {
      color = unique;
      awareness.setLocalStateField("user", { name: userName, color, avatarUrl: avatarUrl ?? null, role: role ?? "viewer" });
    }
    colorResolved = true;
  }

  awareness.setLocalStateField("user", { name: userName, color, avatarUrl: avatarUrl ?? null, role: role ?? "viewer" });

  // Re-pick color once we see other clients' states
  function onFirstChange() {
    pickUniqueColor();
    awareness.off("change", onFirstChange);
  }
  awareness.on("change", onFirstChange);

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
