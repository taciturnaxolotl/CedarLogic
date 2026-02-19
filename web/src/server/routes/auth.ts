import { Database } from "bun:sqlite";
import { Google } from "arctic";
import { upsertUser, findUserById } from "../db/queries/users";
import { claimPendingPermissions } from "../db/queries/files";
import {
  signAccessToken,
  signRefreshToken,
  verifyToken,
  authenticateRequest,
  hashToken,
  parseCookies,
} from "../middleware/auth";

const google = new Google(
  process.env.GOOGLE_CLIENT_ID || "",
  process.env.GOOGLE_CLIENT_SECRET || "",
  process.env.GOOGLE_REDIRECT_URI || "http://localhost:5173/auth/google/callback"
);

function setCookie(name: string, value: string, maxAge: number): string {
  return `${name}=${value}; HttpOnly; SameSite=Lax; Path=/; Max-Age=${maxAge}`;
}

function clearCookie(name: string): string {
  return `${name}=; HttpOnly; SameSite=Lax; Path=/; Max-Age=0`;
}

export async function authRoutes(req: Request, db: Database): Promise<Response> {
  const url = new URL(req.url);

  if (url.pathname === "/auth/google" && req.method === "GET") {
    const state = crypto.randomUUID();
    const codeVerifier = crypto.randomUUID() + crypto.randomUUID();
    const authUrl = google.createAuthorizationURL(state, codeVerifier, [
      "openid",
      "email",
      "profile",
    ]);

    const headers = new Headers();
    headers.set("Location", authUrl.toString());
    headers.append("Set-Cookie", setCookie("oauth_state", state, 600));
    headers.append("Set-Cookie", setCookie("code_verifier", codeVerifier, 600));
    return new Response(null, { status: 302, headers });
  }

  if (url.pathname === "/auth/google/callback" && req.method === "GET") {
    const code = url.searchParams.get("code");
    const state = url.searchParams.get("state");
    const cookies = parseCookies(req.headers.get("Cookie") || "");

    if (!code || !state || state !== cookies["oauth_state"]) {
      return new Response("Invalid OAuth state", { status: 400 });
    }

    try {
      const tokens = await google.validateAuthorizationCode(
        code,
        cookies["code_verifier"] || ""
      );

      // Fetch user info from Google
      const userInfoRes = await fetch(
        "https://www.googleapis.com/oauth2/v2/userinfo",
        { headers: { Authorization: `Bearer ${tokens.accessToken()}` } }
      );
      const userInfo = (await userInfoRes.json()) as {
        id: string;
        email: string;
        name: string;
        picture: string;
      };

      const user = upsertUser(db, {
        googleId: userInfo.id,
        email: userInfo.email,
        name: userInfo.name,
        avatarUrl: userInfo.picture,
      });

      // Claim any pending file permissions
      claimPendingPermissions(db, user.id, user.email);

      const accessToken = await signAccessToken({
        userId: user.id,
        email: user.email,
      });
      const refreshToken = await signRefreshToken({ userId: user.id });

      // Store refresh token hash
      const tokenHash = await hashToken(refreshToken);
      db.query(
        "INSERT INTO refresh_tokens (user_id, token_hash, expires_at) VALUES (?, ?, datetime('now', '+30 days'))"
      ).run(user.id, tokenHash);

      const headers = new Headers();
      headers.set("Location", "/");
      headers.append("Set-Cookie", setCookie("access_token", accessToken, 900));
      headers.append("Set-Cookie", setCookie("refresh_token", refreshToken, 2592000));
      headers.append("Set-Cookie", clearCookie("oauth_state"));
      headers.append("Set-Cookie", clearCookie("code_verifier"));
      return new Response(null, { status: 302, headers });
    } catch (e) {
      console.error("OAuth callback error:", e);
      return new Response("Authentication failed", { status: 500 });
    }
  }

  if (url.pathname === "/auth/refresh" && req.method === "POST") {
    const cookies = parseCookies(req.headers.get("Cookie") || "");
    const refreshToken = cookies["refresh_token"];
    if (!refreshToken) {
      return Response.json({ error: "No refresh token" }, { status: 401 });
    }

    const payload = await verifyToken(refreshToken);
    if (!payload?.sub) {
      return Response.json({ error: "Invalid refresh token" }, { status: 401 });
    }

    const tokenHash = await hashToken(refreshToken);
    const stored = db
      .query(
        "SELECT * FROM refresh_tokens WHERE token_hash = ? AND revoked_at IS NULL AND expires_at > datetime('now')"
      )
      .get(tokenHash) as any;

    if (!stored) {
      return Response.json({ error: "Token revoked or expired" }, { status: 401 });
    }

    // Rotate: revoke old, issue new
    db.query("UPDATE refresh_tokens SET revoked_at = datetime('now') WHERE id = ?").run(stored.id);

    const user = findUserById(db, payload.sub);
    if (!user) {
      return Response.json({ error: "User not found" }, { status: 401 });
    }

    const newAccessToken = await signAccessToken({
      userId: user.id,
      email: user.email,
    });
    const newRefreshToken = await signRefreshToken({ userId: user.id });
    const newHash = await hashToken(newRefreshToken);
    db.query(
      "INSERT INTO refresh_tokens (user_id, token_hash, expires_at) VALUES (?, ?, datetime('now', '+30 days'))"
    ).run(user.id, newHash);

    const headers = new Headers();
    headers.append("Set-Cookie", setCookie("access_token", newAccessToken, 900));
    headers.append("Set-Cookie", setCookie("refresh_token", newRefreshToken, 2592000));
    return new Response(null, { status: 200, headers });
  }

  if (url.pathname === "/auth/logout" && req.method === "POST") {
    const cookies = parseCookies(req.headers.get("Cookie") || "");
    const refreshToken = cookies["refresh_token"];
    if (refreshToken) {
      const tokenHash = await hashToken(refreshToken);
      db.query("UPDATE refresh_tokens SET revoked_at = datetime('now') WHERE token_hash = ?").run(
        tokenHash
      );
    }

    const headers = new Headers();
    headers.append("Set-Cookie", clearCookie("access_token"));
    headers.append("Set-Cookie", clearCookie("refresh_token"));
    return new Response(null, { status: 200, headers });
  }

  // Issue a short-lived token for WebSocket auth (cookies are HttpOnly so JS can't read them)
  if (url.pathname === "/api/auth/ws-token" && req.method === "POST") {
    const auth = await authenticateRequest(req);
    if (!auth) {
      return Response.json({ error: "Unauthorized" }, { status: 401 });
    }
    const token = await signAccessToken({ userId: auth.userId, email: auth.email });
    return Response.json({ token });
  }

  if (url.pathname === "/api/auth/me" && req.method === "GET") {
    const auth = await authenticateRequest(req);
    if (!auth) {
      return Response.json({ user: null }, { status: 401 });
    }

    const user = findUserById(db, auth.userId);
    if (!user) {
      return Response.json({ user: null }, { status: 401 });
    }

    return Response.json({
      user: {
        id: user.id,
        email: user.email,
        name: user.name,
        avatarUrl: user.avatarUrl,
      },
    });
  }

  return new Response("Not Found", { status: 404 });
}
