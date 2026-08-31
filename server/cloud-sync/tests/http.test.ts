import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { once } from "node:events";
import { randomBytes } from "node:crypto";
import { createSyncServer } from "../lib/http.ts";

test("HTTP adapter: authentication, shared result, deduplication, bounds and safe errors", async () => {
  const dir = mkdtempSync(join(tmpdir(), "m5-sync-http-"));
  const original = { ...process.env };
  const writer = randomBytes(32).toString("hex"), reader = randomBytes(32).toString("hex");
  const account = "a".repeat(64);
  Object.assign(process.env, { CODEX_SYNC_TOKEN: writer, CODEX_SYNC_READ_TOKEN: reader,
    CODEX_SYNC_ACCOUNT_SCOPE: account, CODEX_SYNC_DB_PATH: join(dir, "test.sqlite") });
  const server = createSyncServer().listen(0, "127.0.0.1");
  try {
    await once(server, "listening");
    const address = server.address();
    assert.ok(address && typeof address !== "string");
    const endpoint = `http://127.0.0.1:${address.port}/api/rlcd/codex`;
    const request = (token: string, body?: unknown) => fetch(endpoint, {
      method: body === undefined ? "GET" : "POST",
      headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body)
    });
    assert.equal((await request("")).status, 401);
    assert.equal((await request(reader, {})).status, 401);
    const now = Date.now() / 1000;
    const payload = { shared_usage: { version: 1, account_scope: account, device_id: "test-device",
      observations: [{ id: "sample-1", at: now, left: 65, reset_at: now + 86400 }], activity: [] } };
    assert.equal((await request(writer, payload)).status, 200);
    assert.equal((await request(writer, payload)).status, 200);
    const response = await request(reader);
    assert.equal(response.headers.get("cache-control"), "private, no-store");
    const result = await response.json();
    assert.equal(result.shared_usage.weekly.left_pct, 65);
    assert.equal(result.shared_usage.activity_buckets.length, 24);
    assert.equal(result.codex.weekly_left_pct, 65);
    assert.equal((await request(writer, { shared_usage: { ...payload.shared_usage, account_scope: "other" } })).status, 400);
    assert.equal((await request(writer, { dashboard: {} })).status, 400);
    assert.equal((await request(writer, { padding: "x".repeat(2 * 1024 * 1024) })).status, 413);
    const failed = await request(writer, { shared_usage: { ...payload.shared_usage, device_id: writer } });
    // A device ID is opaque; neither valid responses nor errors expose the credential.
    assert.equal((await failed.text()).includes(writer), false);
    assert.equal((await fetch(endpoint.replace("/api/rlcd/codex", "/private"))).status, 404);
    process.env.CODEX_SYNC_TOKEN = "";
    process.env.CODEX_SYNC_READ_TOKEN = "";
    assert.equal((await request(writer)).status, 401);
  } finally {
    server.closeAllConnections();
    await new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
    for (const key of ["CODEX_SYNC_TOKEN", "CODEX_SYNC_READ_TOKEN", "CODEX_SYNC_ACCOUNT_SCOPE", "CODEX_SYNC_DB_PATH"]) {
      if (original[key] === undefined) delete process.env[key]; else process.env[key] = original[key];
    }
    rmSync(dir, { recursive: true, force: true });
  }
});
