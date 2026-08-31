import { createServer, type IncomingMessage } from "node:http";
import { ingestSharedUsage, readSharedUsage, syncAuthorized, syncReadAuthorized } from "./codex-shared-usage.ts";

const MAX_BODY = 2 * 1024 * 1024;
class BodyTooLarge extends Error {}

async function readJSON(request: IncomingMessage) {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of request) {
    const bytes = Buffer.from(chunk);
    size += bytes.length;
    if (size > MAX_BODY) throw new BodyTooLarge();
    chunks.push(bytes);
  }
  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

/** Optional standalone adapter; an existing service can call the same store. */
export function createSyncServer() {
  const server = createServer(async (req, res) => {
    const reply = (status: number, data: unknown) => {
      res.writeHead(status, { "Content-Type": "application/json", "Cache-Control": "private, no-store",
        "Vary": "Authorization", "X-Content-Type-Options": "nosniff" });
      res.end(JSON.stringify(data));
    };
    if (req.url?.split("?")[0] !== "/api/rlcd/codex") { reply(404, { ok: false }); return; }
    if (req.method !== "GET" && req.method !== "POST") { reply(405, { ok: false }); return; }
    const auth = new Request("http://localhost/api/rlcd/codex", {
      headers: { Authorization: req.headers.authorization || "" }
    });
    if (!(req.method === "GET" ? syncReadAuthorized(auth) : syncAuthorized(auth))) {
      reply(401, { ok: false, error: "unauthorized" }); return;
    }
    if (!process.env.CODEX_SYNC_ACCOUNT_SCOPE) { reply(503, { ok: false, error: "not configured" }); return; }
    try {
      const shared = req.method === "POST" ? ingestSharedUsage((await readJSON(req)).shared_usage) : readSharedUsage();
      // Keep a simple quota projection for other authorized displays; no dashboard dependencies.
      reply(200, { ok: true, shared_usage: shared, codex: {
        valid: shared?.weekly.valid ?? false, status: shared?.stale ? "stale" : "ok",
        weekly_left_pct: shared?.weekly.left_pct ?? null, weekly_reset_at: shared?.weekly.reset_at ?? null
      } });
    } catch (error) {
      // Never echo a payload, credential, database path or stack trace to a client.
      reply(error instanceof BodyTooLarge ? 413 : req.method === "POST" ? 400 : 500,
        { ok: false, error: "request rejected" });
    }
  });
  server.requestTimeout = 15_000;
  server.headersTimeout = 10_000;
  return server;
}
