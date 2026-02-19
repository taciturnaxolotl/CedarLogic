// Binary protocol for cursor relay WebSocket
// All messages are raw ArrayBuffers for minimal overhead

export const CURSOR_MOVE = 0x01; // 13 bytes: type(1) + userHash(4) + x(4) + y(4)
export const CURSOR_LEAVE = 0x02; // 5 bytes: type(1) + userHash(4)
export const VIEWPORT_UPDATE = 0x03; // 17 bytes: type(1) + minX(4) + minY(4) + maxX(4) + maxY(4)
export const AUTH = 0x04; // variable: text message "roomId:token"

// Hash a userId string to a stable uint32
export function hashUserId(userId: string): number {
  let h = 0x811c9dc5; // FNV offset basis
  for (let i = 0; i < userId.length; i++) {
    h ^= userId.charCodeAt(i);
    h = Math.imul(h, 0x01000193); // FNV prime
  }
  return h >>> 0; // ensure unsigned
}

// --- Encode helpers ---

export function encodeCursorMove(
  userHash: number,
  x: number,
  y: number,
): ArrayBuffer {
  const buf = new ArrayBuffer(13);
  const view = new DataView(buf);
  view.setUint8(0, CURSOR_MOVE);
  view.setUint32(1, userHash, true);
  view.setFloat32(5, x, true);
  view.setFloat32(9, y, true);
  return buf;
}

export function encodeCursorLeave(userHash: number): ArrayBuffer {
  const buf = new ArrayBuffer(5);
  const view = new DataView(buf);
  view.setUint8(0, CURSOR_LEAVE);
  view.setUint32(1, userHash, true);
  return buf;
}

export function encodeViewportUpdate(
  minX: number,
  minY: number,
  maxX: number,
  maxY: number,
): ArrayBuffer {
  const buf = new ArrayBuffer(17);
  const view = new DataView(buf);
  view.setUint8(0, VIEWPORT_UPDATE);
  view.setFloat32(1, minX, true);
  view.setFloat32(5, minY, true);
  view.setFloat32(9, maxX, true);
  view.setFloat32(13, maxY, true);
  return buf;
}

// --- Decode helpers ---

export interface CursorMoveMsg {
  type: typeof CURSOR_MOVE;
  userHash: number;
  x: number;
  y: number;
}

export interface CursorLeaveMsg {
  type: typeof CURSOR_LEAVE;
  userHash: number;
}

export interface ViewportUpdateMsg {
  type: typeof VIEWPORT_UPDATE;
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
}

export type CursorMessage = CursorMoveMsg | CursorLeaveMsg | ViewportUpdateMsg;

export function decodeMessage(data: ArrayBuffer): CursorMessage | null {
  const view = new DataView(data);
  const type = view.getUint8(0);

  switch (type) {
    case CURSOR_MOVE:
      return {
        type: CURSOR_MOVE,
        userHash: view.getUint32(1, true),
        x: view.getFloat32(5, true),
        y: view.getFloat32(9, true),
      };
    case CURSOR_LEAVE:
      return {
        type: CURSOR_LEAVE,
        userHash: view.getUint32(1, true),
      };
    case VIEWPORT_UPDATE:
      return {
        type: VIEWPORT_UPDATE,
        minX: view.getFloat32(1, true),
        minY: view.getFloat32(5, true),
        maxX: view.getFloat32(9, true),
        maxY: view.getFloat32(13, true),
      };
    default:
      return null;
  }
}
