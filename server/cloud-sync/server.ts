import { createSyncServer } from "./lib/http.ts";

const account = process.env.CODEX_SYNC_ACCOUNT_SCOPE || "";
const token = process.env.CODEX_SYNC_TOKEN || "";
const readToken = process.env.CODEX_SYNC_READ_TOKEN || "";
const port = Number(process.env.PORT || 4199);
if (!/^[a-f0-9]{64}$/.test(account) || token.length < 32 ||
    (readToken && (readToken.length < 32 || readToken === token)) ||
    !Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error("Configure account scope, distinct private credentials and a valid port before starting.");
}

// Keep direct access local. A separately configured HTTPS reverse proxy is required for Macs.
createSyncServer().listen(port, "127.0.0.1", () => console.log("StopWatch cloud sync listening on loopback."));
