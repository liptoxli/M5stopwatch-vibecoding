import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { SharedUsageStore, dailyUsage, heatmap, validateBatch } from "../lib/codex-shared-usage.ts";

const account = "test-account", day = Date.parse("2026-09-01T00:00:00Z") / 1000;
const reset = day + 3 * 86400;
const batch = (device: string, observations: object[] = [], activity: object[] = []) => ({
  version: 1, account_scope: account, device_id: device, observations, activity
});
const obs = (id: string, at: number, left: number, reset_at: number | null = reset) => ({ id, at, left, reset_at });
const row = (at: number, left: number, reset_at: number | null = reset) => ({ ...obs(String(at), at, left, reset_at), device: "test" });

test("two Macs share Today, and repeated/global observations are not added twice", () => {
  const s = new SharedUsageStore(":memory:");
  s.ingest(batch("mini", [obs("a", day, 98), obs("b", day + 3600, 91)]), account, day + 7200);
  const imac = batch("imac", [obs("c", day + 4000, 91), obs("d", day + 7000, 89)]);
  s.ingest(imac, account, day + 7200);
  const result = s.ingest(imac, account, day + 7200);
  assert.equal(result.weekly.left_pct, 89);
  assert.equal(result.daily_tracking.used_since_start_pct_points, 9);
  assert.equal(result.daily_tracking.day_start_left_pct, 98);
  assert.equal(result.daily_tracking.timezone, "Asia/Shanghai");
  s.close();
});
test("late offline upload cannot replace a fresh remaining quota", () => {
  const s = new SharedUsageStore(":memory:");
  s.ingest(batch("imac", [obs("new", day + 7000, 89)]), account, day + 7200);
  const r = s.ingest(batch("mini", [obs("old", day, 98)]), account, day + 7200);
  assert.equal(r.weekly.left_pct, 89);
  assert.equal(r.daily_tracking.used_since_start_pct_points, 9);
  s.close();
});
test("missing 08:00 baseline is unknown, not a freshly reset zero", () => {
  const r = dailyUsage([row(day + 3600, 89)], day + 4000);
  assert.equal(r.quality, "partial");
  assert.equal(r.day_start_left_pct, -1);
  assert.equal(dailyUsage([], day).quality, "missing");
});
test("imported local baselines are reconciled once and labelled estimated", () => {
  const r = dailyUsage([{ ...row(day, 98), id: "legacy-baseline-a", partial: true }, { ...row(day, 89), id: "legacy-baseline-b", partial: true }, row(day + 4000, 89)], day + 5000);
  assert.equal(r.quality, "imported");
  assert.equal(r.day_start_left_pct, 98);
  assert.equal(r.used_since_start_pct_points, 9);
});
test("a second Mac's late local baseline is a candidate, not a midnight consumption event", () => {
  const r = dailyUsage([
    { ...row(day, 98), id: "legacy-baseline-mini", partial: true },
    { ...row(day, 89), id: "legacy-baseline-imac", partial: true },
    { ...row(day + 3600, 91), id: "legacy-latest-mini", partial: true },
    { ...row(day + 7200, 89), id: "legacy-latest-imac", partial: true },
    row(day + 8000, 87)
  ], day + 8100);
  assert.equal(r.quality, "imported");
  assert.equal(r.used_since_start_pct_points, 11);
});
test("a real boundary sample takes precedence over later imported local baselines", () => {
  const r = dailyUsage([row(day - 30, 97), { ...row(day, 89), id: "legacy-baseline-imac", partial: true }, row(day + 100, 96)], day + 200);
  assert.equal(r.quality, "sampled");
  assert.equal(r.used_since_start_pct_points, 1);
});
test("boundary uses recent previous-day sample and rolls at Beijing 08:00", () => {
  const r = dailyUsage([row(day - 30, 95), row(day + 100, 93)], day + 200);
  assert.equal(r.used_since_start_pct_points, 2);
  assert.equal(r.day_key, "2026-09-01");
  assert.equal(dailyUsage([row(day - 30, 95)], day - 1).day_key, "2026-08-31");
  assert.equal(dailyUsage([row(day + 3600, 93)], day + 86401).quality, "missing");
});
test("official reset crossing adds only the new cycle consumption", () => {
  const r = dailyUsage([row(day, 10, day + 600), row(day + 300, 8, day + 600), row(day + 900, 97, day + 7 * 86400)], day + 1000);
  assert.equal(r.used_since_start_pct_points, 5);
  assert.equal(r.reset_count, 1);
});
test("upward stale echo does not cause a second charge", () => {
  const r = dailyUsage([row(day, 98), row(day + 400, 91), row(day + 410, 95), row(day + 430, 91)], day + 500);
  assert.equal(r.used_since_start_pct_points, 7);
  assert.equal(r.reset_count, 0);
});
test("sustained unexplained quota increase is not silently treated as a refill", () => {
  const r = dailyUsage([row(day, 30), row(day + 60, 80), row(day + 120, 79), row(day + 180, 78)], day + 200);
  assert.equal(r.quality, "partial");
  assert.equal(r.day_start_left_pct, -1);
  assert.equal(r.reset_count, 0);
});
test("shared statistics survive store reopen and alternate database connections", () => {
  const dir = mkdtempSync(join(tmpdir(), "m5-cloud-db-")), path = join(dir, "shared.sqlite");
  try {
    const a = new SharedUsageStore(path), b = new SharedUsageStore(path);
    a.ingest(batch("mini", [obs("first", day, 98)]), account, day + 100);
    b.ingest(batch("imac", [obs("second", day + 50, 89)]), account, day + 100);
    a.close(); b.close();
    const reopened = new SharedUsageStore(path);
    assert.equal(reopened.snapshot(account, day + 100).daily_tracking.used_since_start_pct_points, 9);
    reopened.close();
  } finally { rmSync(dir, { recursive: true }); }
});
test("invalid values, wrong account and unreasonable clock skew are rejected atomically", () => {
  for (const o of [obs("bad", day, -1), obs("bad", day, 101), obs("bad", day + 200, 50), obs("bad", day, NaN)]) {
    assert.throws(() => validateBatch(batch("mini", [o]), account, day));
  }
  assert.throws(() => validateBatch(batch("mini"), "other", day));
  const s = new SharedUsageStore(":memory:");
  assert.throws(() => s.ingest(batch("mini", [obs("ok", day, 90), obs("bad", day, 101)]), account, day));
  assert.equal(s.snapshot(account, day).weekly.valid, false);
  s.close();
});
test("activity sessions union across Macs, retry dedup, checkpoints only extend", () => {
  const s = new SharedUsageStore(":memory:"), now = day + 1000;
  const a = { id: "rec", kind: "recording", start: now - 600, end: now - 100, units: 0 };
  s.ingest(batch("mini", [], [a]), account, now);
  s.ingest(batch("imac", [], [{ ...a, start: now - 500, end: now }]), account, now);
  const r = s.ingest(batch("mini", [], [a]), account, now);
  assert.equal(r.activity_buckets.length, 24);
  assert.equal(r.activity_buckets[23], .78);
  const checkpoint = s.ingest(batch("mini", [], [{ ...a, end: now - 200 }]), account, now);
  assert.deepEqual(checkpoint.activity_buckets, r.activity_buckets);
  assert.deepEqual(s.snapshot(account, now + 14401).activity_buckets, Array(24).fill(0));
  s.close();
});
test("heatmap includes real interactions, never quota polls", () => {
  const r = heatmap([{ id: "a", kind: "interaction", start: day - 1, end: day - 1, units: .25 }], day);
  assert.equal(r[23], .02);
});
