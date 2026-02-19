export interface User {
  id: string;
  googleId: string;
  email: string;
  name: string;
  avatarUrl: string | null;
  createdAt: string;
  updatedAt: string;
}

export interface PublicUser {
  id: string;
  email: string;
  name: string;
  avatarUrl: string | null;
}

export interface FileRecord {
  id: string;
  ownerId: string;
  title: string;
  linkSharing: "private" | "viewer" | "editor";
  createdAt: string;
  updatedAt: string;
}

export interface FilePermission {
  id: string;
  fileId: string;
  userEmail: string;
  userId: string | null;
  role: "viewer" | "editor";
  createdAt: string;
}

export type PermissionLevel = "owner" | "editor" | "viewer" | null;

export interface FileWithPermission extends FileRecord {
  permission: PermissionLevel;
  ownerName?: string;
}

export interface AuthContext {
  userId: string;
  email: string;
}

export interface WireSegment {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

export interface GateDefinition {
  id: string;
  name: string;
  caption: string;
  library: string;
  logicType: string;
  guiType: string;
  params: Record<string, string>;
  guiParams: Record<string, string>;
  inputs: Array<{ name: string; x: number; y: number }>;
  outputs: Array<{ name: string; x: number; y: number }>;
  shape: Array<{ x1: number; y1: number; x2: number; y2: number }>;
}
