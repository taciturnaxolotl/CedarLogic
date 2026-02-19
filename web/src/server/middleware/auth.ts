import * as jose from "jose";
import type { AuthContext } from "@shared/types";

const JWT_SECRET = new TextEncoder().encode(
  process.env.JWT_SECRET || "cedarlogic-dev-secret-change-in-production"
);

export async function signAccessToken(payload: { userId: string; email: string }): Promise<string> {
  return new jose.SignJWT({ sub: payload.userId, email: payload.email })
    .setProtectedHeader({ alg: "HS256" })
    .setIssuedAt()
    .setExpirationTime("15m")
    .sign(JWT_SECRET);
}

export async function signRefreshToken(payload: { userId: string }): Promise<string> {
  return new jose.SignJWT({ sub: payload.userId })
    .setProtectedHeader({ alg: "HS256" })
    .setIssuedAt()
    .setExpirationTime("30d")
    .sign(JWT_SECRET);
}

export async function verifyToken(token: string): Promise<jose.JWTPayload | null> {
  try {
    const { payload } = await jose.jwtVerify(token, JWT_SECRET);
    return payload;
  } catch {
    return null;
  }
}

export async function authenticateRequest(req: Request): Promise<AuthContext | null> {
  // Check Authorization header
  const authHeader = req.headers.get("Authorization");
  if (authHeader?.startsWith("Bearer ")) {
    const payload = await verifyToken(authHeader.slice(7));
    if (payload?.sub && payload.email) {
      return { userId: payload.sub, email: payload.email as string };
    }
  }

  // Check cookie
  const cookies = parseCookies(req.headers.get("Cookie") || "");
  const accessToken = cookies["access_token"];
  if (accessToken) {
    const payload = await verifyToken(accessToken);
    if (payload?.sub && payload.email) {
      return { userId: payload.sub, email: payload.email as string };
    }
  }

  return null;
}

export function parseCookies(cookieHeader: string): Record<string, string> {
  const cookies: Record<string, string> = {};
  for (const pair of cookieHeader.split(";")) {
    const [key, ...rest] = pair.split("=");
    if (key) cookies[key.trim()] = rest.join("=").trim();
  }
  return cookies;
}

export async function hashToken(token: string): Promise<string> {
  const data = new TextEncoder().encode(token);
  const hash = await crypto.subtle.digest("SHA-256", data);
  return Buffer.from(hash).toString("hex");
}
