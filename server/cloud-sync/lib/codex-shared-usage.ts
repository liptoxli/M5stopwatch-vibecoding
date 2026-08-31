// Account-wide statistics behind the existing Codex API. No audio, text or
// OpenAI credentials enter this store. SQLite transactions make retries and
// overlapping requests from different Macs safe across server workers.
import { createHash, timingSafeEqual } from "node:crypto";
import { chmodSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { DatabaseSync } from "node:sqlite";

const DAY = 86400;
const WINDOW = 14400;
const RETENTION = 8 * DAY;
type Observation = { id: string; at: number; left: number; reset_at: number | null; partial?: boolean };
type Activity = { id: string; kind: "recording" | "interaction"; start: number; end: number; units: number };
export type SyncBatch = { version: number; account_scope: string; device_id: string; observations: Observation[]; activity: Activity[] };
type Row = Observation & { device: string };

export function configuredAccount() { return process.env.CODEX_SYNC_ACCOUNT_SCOPE || ""; }
function tokenMatches(request: Request, expected: string) {
  const actual = request.headers.get("authorization") || "";
  const bytes = Buffer.from(`Bearer ${expected}`);
  return expected.length >= 32 && Buffer.byteLength(actual) === bytes.length &&
    timingSafeEqual(bytes, Buffer.from(actual));
}
export function syncAuthorized(request: Request) { return tokenMatches(request, process.env.CODEX_SYNC_TOKEN || ""); }
export function syncReadAuthorized(request: Request) {
  return syncAuthorized(request) || tokenMatches(request, process.env.CODEX_SYNC_READ_TOKEN || "");
}

function safeID(value: unknown, max = 100): value is string {
  return typeof value === "string" && value.length > 0 && value.length <= max && /^[a-zA-Z0-9_.:-]+$/.test(value);
}
function finite(value: unknown): value is number { return typeof value === "number" && Number.isFinite(value); }

export function validateBatch(value: unknown, account: string, now: number): SyncBatch {
  const b = value as SyncBatch;
  if (!b || b.version !== 1 || !account || b.account_scope !== account || !safeID(b.device_id) ||
      !Array.isArray(b.observations) || b.observations.length > 3000 ||
      !Array.isArray(b.activity) || b.activity.length > 4000) throw new Error("invalid sync batch");
  const observations = b.observations.map(o => {
    if (!o || !safeID(o.id) || !finite(o.at) || o.at > now + 120 || o.at < now - RETENTION ||
        !finite(o.left) || o.left < 0 || o.left > 100 ||
        (o.reset_at !== null && (!finite(o.reset_at) || o.reset_at < o.at - 120 || o.reset_at > o.at + 9 * DAY))) {
      throw new Error("invalid quota observation");
    }
    return { id: o.id, at: o.at, left: o.left, reset_at: o.reset_at, partial: o.partial === true };
  });
  const activity = b.activity.map(a => {
    if (!a || !safeID(a.id) || !["recording", "interaction"].includes(a.kind) ||
        !finite(a.start) || !finite(a.end) || a.start > a.end || a.end > now + 120 ||
        a.end < now - RETENTION || a.start < now - RETENTION ||
        !finite(a.units) || a.units < 0 || a.units > 1 ||
        (a.kind === "interaction" && a.end !== a.start)) throw new Error("invalid activity");
    return { id: a.id, kind: a.kind, start: a.start, end: a.end, units: a.units };
  });
  return { version: 1, account_scope: account, device_id: b.device_id, observations, activity };
}

export function heatmap(events: Activity[], now: number): number[] {
  // Union overlapping recording intervals: one physical recording must not
  // count twice during a host handover. Distinct touching sessions stay distinct.
  const intervals: {start: number; end: number}[] = [];
  for (const e of events.filter(e => e.kind === "recording").sort((a, b) => a.start - b.start)) {
    const last = intervals.at(-1);
    if (last && e.start < last.end) last.end = Math.max(last.end, e.end);
    else intervals.push({ start: e.start, end: e.end });
  }
  return Array.from({ length: 24 }, (_, i) => {
    const start = now - WINDOW + i * 600, end = start + 600;
    let duration = 0, starts = 0, units = 0;
    for (const e of intervals) {
      duration += Math.max(0, Math.min(end, e.end) - Math.max(start, e.start));
      if (e.start >= start && e.start < end) starts++;
    }
    for (const e of events) if (e.kind === "interaction" && e.start >= start && e.start < end) units += e.units;
    return Math.round((Math.min(1, duration / 600) * .7 + Math.min(1, (starts + units) / 4) * .3 + Number.EPSILON) * 100) / 100;
  });
}

export function dailyUsage(input: Row[], now: number) {
  // Beijing 08:00 is UTC 00:00, independent of the collecting Mac's timezone.
  const start = Math.floor(now / DAY) * DAY;
  const rows = input.filter(r => r.at <= now).sort((a, b) => a.at - b.at || b.left - a.left);
  const today = rows.filter(r => r.at >= start);
  const before = rows.filter(r => !r.partial && r.at < start && start - r.at <= 600).at(-1);
  const boundary = before || today.find(r => !r.partial && r.at - start <= 600);
  const isImportedBaseline = (r: Row) => r.partial && r.id.startsWith("legacy-baseline-");
  // A legacy baseline was observed at an unknown time. Multiple imports are
  // candidate day anchors, NOT separate consumption events at 08:00.
  const imported = today.filter(isImportedBaseline).sort((a, b) => b.left - a.left)[0];
  const base = boundary || imported || today[0];
  let quality = base?.partial ? "imported" : base && Math.abs(base.at - start) <= 600 ? "sampled" : "partial";
  let used = 0, resets = 0;
  let previous = base;
  let floor = base?.left ?? 0;
  let floorAt = base?.at ?? now;
  let segment = floor;
  if (base) for (const row of today) {
    if (row === base || row.at < base.at) continue;
    if (isImportedBaseline(row) || (boundary && row.partial)) continue;
    const oldReset = previous?.reset_at;
    const newReset = row.reset_at;
    if (oldReset && newReset && Math.abs(newReset - oldReset) > 120) {
      // Only an actual crossed official reset allows counting from 100.
      // Other changes (credits/manual reset/plan corrections) are discontinuities.
      if (oldReset >= start && oldReset <= row.at && newReset > oldReset) used += 100 - row.left;
      else quality = "partial";
      resets++;
      floor = row.left;
      floorAt = row.at;
      segment = row.left;
    } else if (row.left < floor) {
      used += floor - row.left;
      floor = row.left;
      floorAt = row.at;
    } else if (row.left === floor) {
      floorAt = row.at;
    } else if (row.left > floor + 1 && row.at - floorAt > 120) {
      quality = "partial";
      // Do not turn a stale upward echo into a refill and count its next fall twice.
    }
    if (row.partial && quality === "sampled") quality = "imported";
    previous = row;
  }
  if (!base) quality = "missing";
  return {
    boundary_hour_local: 8, timezone: "Asia/Shanghai",
    day_key: new Date(start * 1000).toISOString().slice(0, 10), period_start: new Date(start * 1000).toISOString(),
    // Existing watch firmware already understands -1 as Today unknown.
    day_start_left_pct: quality === "sampled" || quality === "imported" ? Math.round(base!.left) : -1,
    observed_start_left_pct: base?.left ?? null,
    segment_start_left_pct: base ? Math.round(segment) : -1,
    current_left_pct: rows.at(-1)?.left ?? null,
    used_since_start_pct_points: Math.round(used * 100) / 100,
    reset_count: resets, quality, updated_at: new Date((rows.at(-1)?.at ?? now) * 1000).toISOString()
  };
}

export class SharedUsageStore {
  private db: DatabaseSync;
  constructor(path: string) {
    if (path !== ":memory:") mkdirSync(dirname(path), { recursive: true, mode: 0o700 });
    this.db = new DatabaseSync(path);
    if (path !== ":memory:") chmodSync(path, 0o600);
    this.db.exec(`PRAGMA journal_mode=WAL; PRAGMA busy_timeout=3000;
      CREATE TABLE IF NOT EXISTS quota(account TEXT, device TEXT, id TEXT, at REAL, left_pct REAL, reset_at REAL, partial INTEGER, PRIMARY KEY(account,device,id));
      CREATE INDEX IF NOT EXISTS quota_time ON quota(account,at);
      CREATE TABLE IF NOT EXISTS activity(account TEXT, device TEXT, id TEXT, kind TEXT, start REAL, end REAL, units REAL, PRIMARY KEY(account,device,id));
      CREATE INDEX IF NOT EXISTS activity_time ON activity(account,end);`);
  }
  close() { this.db.close(); }
  ingest(value: unknown, account: string, now = Date.now() / 1000) {
    const batch = validateBatch(value, account, now);
    this.db.exec("BEGIN IMMEDIATE");
    try {
      const quota = this.db.prepare("INSERT OR IGNORE INTO quota VALUES(?,?,?,?,?,?,?)");
      for (const o of batch.observations) quota.run(account, batch.device_id, o.id, o.at, o.left, o.reset_at, o.partial ? 1 : 0);
      const event = this.db.prepare(`INSERT INTO activity VALUES(?,?,?,?,?,?,?) ON CONFLICT(account,device,id) DO UPDATE SET end=MAX(activity.end,excluded.end)
        WHERE activity.kind=excluded.kind AND activity.start=excluded.start AND activity.units=excluded.units`);
      for (const a of batch.activity) event.run(account, batch.device_id, a.id, a.kind, a.start, a.end, a.units);
      this.db.prepare("DELETE FROM quota WHERE at < ?").run(now - RETENTION);
      this.db.prepare("DELETE FROM activity WHERE end < ?").run(now - RETENTION);
      this.db.exec("COMMIT");
    } catch (error) { this.db.exec("ROLLBACK"); throw error; }
    return this.snapshot(account, now);
  }
  snapshot(account: string, now = Date.now() / 1000) {
    this.db.exec("BEGIN");
    try { return this.readSnapshot(account, now); }
    finally { this.db.exec("ROLLBACK"); } // Read transaction only.
  }
  private readSnapshot(account: string, now: number) {
    const day = Math.floor(now / DAY) * DAY;
    const rows = this.db.prepare("SELECT device,id,at,left_pct AS left,reset_at,partial FROM quota WHERE account=? AND at>=? AND at<=? ORDER BY at,left_pct DESC")
      .all(account, day - 600, now).map(r => ({ device: String(r.device), id: String(r.id), at: Number(r.at),
        left: Number(r.left), reset_at: r.reset_at === null ? null : Number(r.reset_at), partial: Boolean(r.partial) }));
    const latestRow = this.db.prepare("SELECT at,left_pct AS left,reset_at FROM quota WHERE account=? AND at<=? ORDER BY at DESC,left_pct ASC LIMIT 1").get(account, now);
    const latest = latestRow ? { at: Number(latestRow.at), left: Number(latestRow.left), reset_at: latestRow.reset_at === null ? null : Number(latestRow.reset_at) } : undefined;
    const events: Activity[] = this.db.prepare("SELECT id,kind,start,end,units FROM activity WHERE account=? AND end>=? AND start<=?").all(account, now - WINDOW, now)
      .map(r => ({ id: String(r.id), kind: r.kind === "recording" ? "recording" : "interaction", start: Number(r.start), end: Number(r.end), units: Number(r.units) }));
    return {
      version: 1, as_of: now, stale: !latest || now - latest.at > 600,
      weekly: { valid: Boolean(latest), left_pct: latest?.left ?? null, reset_at: latest?.reset_at ?? null, observed_at: latest?.at ?? null },
      daily_tracking: dailyUsage(rows, now),
      activity_window_seconds: WINDOW, activity_buckets: heatmap(events, now)
    };
  }
}

function withStore<T>(action: (store: SharedUsageStore) => T): T {
  const cache = process.env.CODEX_SYNC_DATA_DIR || "data";
  const store = new SharedUsageStore(process.env.CODEX_SYNC_DB_PATH || join(cache, "codex-shared-usage.sqlite"));
  try { return action(store); } finally { store.close(); }
}
export function readSharedUsage() {
  const account = configuredAccount();
  return account ? withStore(s => s.snapshot(account)) : null;
}
export function ingestSharedUsage(value: unknown) {
  return withStore(s => s.ingest(value, configuredAccount()));
}

export function ingestLegacyQuota(payload: { account_scope?: string; device_id?: string; dashboard?: { codex?: Record<string, unknown> } }) {
  const account = configuredAccount(), c = payload.dashboard?.codex;
  if (!account || payload.account_scope !== account || c?.valid !== true || c.status !== "ok" || !finite(c.weekly_left_pct)) return;
  const at = typeof c.updated_at === "string" ? Date.parse(c.updated_at) / 1000 : NaN;
  if (!finite(at)) return;
  const reset = finite(c.weekly_reset_at) ? c.weekly_reset_at : null;
  const id = createHash("sha256").update(`${at}:${c.weekly_left_pct}:${reset}`).digest("hex");
  return ingestSharedUsage({ version: 1, account_scope: account, device_id: `collector:${payload.device_id || "legacy"}`,
    observations: [{ id, at, left: c.weekly_left_pct, reset_at: reset }], activity: [] });
}
