import Foundation

@main struct CloudSyncTests {
    static func main() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("m5-cloud-test-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let account = "test-only-account", now = Date(timeIntervalSince1970: 1788228000)
        let start = floor(now.timeIntervalSince1970 / 86400) * 86400
        let dayKey = String(ISO8601DateFormatter().string(from: Date(timeIntervalSince1970: start)).prefix(10))
        let config = CloudSyncConfiguration(endpoint: "https://example.invalid/api/rlcd/codex", token: String(repeating: "x", count: 40),
                                            account_scope: codexSyncAccountScope(account), device_id: "test-mac")
        try JSONEncoder().encode(config).write(to: dir.appendingPathComponent("cloud-sync.json"))
        var sampleCount = 0, failed = true, requestCount = 0, lastObservationCount = 0
        var responseTime = now.timeIntervalSince1970
        let client = CloudUsageSynchronizer(directory: dir) { request in
            requestCount += 1
            let body = try JSONSerialization.jsonObject(with: request.httpBody!) as! [String: Any]
            let batch = body["shared_usage"] as! [String: Any]
            lastObservationCount = (batch["observations"] as! [Any]).count
            assert(request.url?.scheme == "https")
            assert(!String(data: request.httpBody!, encoding: .utf8)!.contains(account))
            assert(batch["account_scope"] as? String == codexSyncAccountScope(account))
            if failed { throw CloudSyncError.timeout }
            let snapshot: [String: Any] = [
                "version": 1, "as_of": responseTime, "stale": false,
                "weekly": ["valid": true, "left_pct": 89, "reset_at": start + 86400, "observed_at": now.timeIntervalSince1970],
                "daily_tracking": ["day_key": dayKey, "period_start": ISO8601DateFormatter().string(from: Date(timeIntervalSince1970: start)),
                    "day_start_left_pct": 98, "segment_start_left_pct": 98, "used_since_start_pct_points": 9,
                    "reset_count": 0, "quality": "sampled", "updated_at": ISO8601DateFormatter().string(from: now)],
                "activity_window_seconds": 14400, "activity_buckets": Array(repeating: 0.0, count: 24)
            ]
            return try JSONSerialization.data(withJSONObject: ["ok": true, "shared_usage": snapshot])
        }
        let sample = { () throws -> CloudQuotaObservation in
            sampleCount += 1
            return CloudQuotaObservation(at: now.timeIntervalSince1970, left: 89, reset_at: start + 86400)
        }
        do { _ = try client.refresh(accountID: account, activity: [], now: now, sample: sample); assertionFailure("offline without cache") }
        catch { assert(sampleCount == 1) }
        assert(lastObservationCount == 1)
        failed = false
        let result = try client.refresh(accountID: account, activity: [], now: now.addingTimeInterval(60), sample: sample)
        assert(lastObservationCount == 1 && sampleCount == 1, "retry retained the exact sample")
        assert(result.daily_tracking.used_since_start_pct_points == 9 && result.weekly.left_pct == 89)
        _ = try client.refresh(accountID: account, activity: [], now: now.addingTimeInterval(120), sample: sample)
        assert(lastObservationCount == 0, "acknowledgement drained the outbox")
        let calls = requestCount
        do { _ = try client.refresh(accountID: "other-account", activity: [], now: now, sample: sample); assertionFailure("account mismatch") }
        catch { assert(requestCount == calls, "wrong account must not make an HTTP request") }
        failed = true
        let cached = try client.refresh(accountID: account, activity: [], now: now.addingTimeInterval(400), sample: sample)
        assert(cached.stale && cached.weekly.left_pct == 89 && lastObservationCount == 1)
        let nextDay = cached.forDisplay(now: Date(timeIntervalSince1970: start + 86401), offline: true)
        assert(nextDay.daily_tracking.day_start_left_pct == -1, "yesterday's Today is not today's value")
        // A delayed/stale relay response must not move shared history backwards
        // or acknowledge samples it could not yet have included.
        failed = false; responseTime = now.timeIntervalSince1970 - 1000
        let oldResponse = try client.refresh(accountID: account, activity: [], now: now.addingTimeInterval(460), sample: sample)
        assert(oldResponse.as_of == cached.as_of && oldResponse.stale, "old response cannot replace cache")
        print("PASS cloud sync: retry, acknowledgement, account scope, cache, day boundary, stale response")
    }
}
