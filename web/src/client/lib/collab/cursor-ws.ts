import {
  encodeCursorMove,
  encodeCursorLeave,
  encodeViewportUpdate,
  decodeMessage,
  CURSOR_MOVE,
  CURSOR_LEAVE,
  type CursorMoveMsg,
} from "@/server/cursor/protocol";

export interface CursorWSCallbacks {
  onCursorMove?: (userHash: number, x: number, y: number) => void;
  onCursorLeave?: (userHash: number) => void;
}

export class CursorWS {
  private ws: WebSocket | null = null;
  private roomId: string;
  private token: string;
  private userHash: number;
  private callbacks: CursorWSCallbacks = {};
  private destroyed = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private lastCursorSend = 0;
  private lastViewportSend = 0;

  constructor(roomId: string, token: string, userHash: number) {
    this.roomId = roomId;
    this.token = token;
    this.userHash = userHash;
    this.connect();
  }

  on(callbacks: CursorWSCallbacks) {
    this.callbacks = callbacks;
  }

  private connect() {
    if (this.destroyed) return;

    const wsUrl = import.meta.env.PROD
      ? `${window.location.protocol === "https:" ? "wss:" : "ws:"}//${window.location.host}/cursor-ws`
      : `ws://${window.location.hostname}:3002`;

    const ws = new WebSocket(wsUrl);
    ws.binaryType = "arraybuffer";

    ws.onopen = () => {
      // First message: auth
      ws.send(`${this.roomId}:${this.token}`);
    };

    ws.onmessage = (e) => {
      if (!(e.data instanceof ArrayBuffer)) return;
      const msg = decodeMessage(e.data);
      if (!msg) return;

      switch (msg.type) {
        case CURSOR_MOVE:
          this.callbacks.onCursorMove?.(msg.userHash, msg.x, msg.y);
          break;
        case CURSOR_LEAVE:
          this.callbacks.onCursorLeave?.(msg.userHash);
          break;
      }
    };

    ws.onclose = () => {
      this.ws = null;
      if (!this.destroyed) {
        this.reconnectTimer = setTimeout(() => this.connect(), 1000);
      }
    };

    ws.onerror = () => {
      ws.close();
    };

    this.ws = ws;
  }

  sendCursorMove(x: number, y: number) {
    const now = performance.now();
    if (now - this.lastCursorSend < 16) return; // 60fps throttle
    this.lastCursorSend = now;

    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(encodeCursorMove(this.userHash, x, y));
    }
  }

  sendViewportUpdate(minX: number, minY: number, maxX: number, maxY: number) {
    const now = performance.now();
    if (now - this.lastViewportSend < 200) return; // 5fps throttle
    this.lastViewportSend = now;

    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(encodeViewportUpdate(minX, minY, maxX, maxY));
    }
  }

  sendCursorLeave() {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(encodeCursorLeave(this.userHash));
    }
  }

  destroy() {
    this.destroyed = true;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.ws?.close();
    this.ws = null;
  }
}
