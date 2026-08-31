import CryptoKit
import Foundation

func codexSyncAccountScope(_ accountID: String) -> String {
    SHA256.hash(data: Data("m5-codex-account-v1:\(accountID)".utf8)).map { String(format: "%02x", $0) }.joined()
}

struct CloudQuotaObservation: Codable {
    var id: String = UUID().uuidString
    var at: Double
    var left: Double
    var reset_at: Double?
    var partial: Bool? = nil
    // JSON must contain null, rather than omit reset_at, for the versioned API.
    enum CodingKeys: String, CodingKey { case id, at, left, reset_at, partial }
    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(id, forKey: .id); try c.encode(at, forKey: .at); try c.encode(left, forKey: .left)
        if let reset_at { try c.encode(reset_at, forKey: .reset_at) } else { try c.encodeNil(forKey: .reset_at) }
        try c.encodeIfPresent(partial, forKey: .partial)
    }
}

struct CloudActivityEvent: Codable {
    var id: String
    var kind: String
    var start: Double
    var end: Double
    var units: Double

    static func make(kind: String, start: Date, end: Date, units: Double = 0) -> Self {
        let stamp = String(format: "%.6f", start.timeIntervalSince1970)
        let id = SHA256.hash(data: Data("\(kind):\(stamp):\(units)".utf8)).map { String(format: "%02x", $0) }.joined()
        return Self(id: id, kind: kind, start: start.timeIntervalSince1970, end: end.timeIntervalSince1970, units: units)
    }
}

struct CloudSyncConfiguration: Codable {
    var endpoint: String
    var token: String
    var account_scope: String
    var device_id: String
}

struct CloudUsageSnapshot: Codable {
    struct Weekly: Codable { var valid: Bool; var left_pct: Double?; var reset_at: Double?; var observed_at: Double? }
    struct Daily: Codable {
        var day_key: String
        var period_start: String
        var day_start_left_pct: Int
        var segment_start_left_pct: Int
        var used_since_start_pct_points: Double
        var reset_count: Int
        var quality: String
        var updated_at: String
        var payload: [String: Any] { [
            "boundary_hour_local": 8, "timezone": "Asia/Shanghai", "day_key": day_key,
            "period_start": period_start, "day_start_left_pct": day_start_left_pct,
            "segment_start_left_pct": segment_start_left_pct,
            "used_since_start_pct_points": Int(used_since_start_pct_points.rounded()),
            "reset_count": reset_count, "quality": quality, "updated_at": updated_at
        ] }
    }
    var version: Int
    var as_of: Double
    var stale: Bool
    var weekly: Weekly
    var daily_tracking: Daily
    var activity_window_seconds: Int
    var activity_buckets: [Double]

    func validated(now: Date) throws -> Self {
        guard version == 1, as_of.isFinite, as_of <= now.timeIntervalSince1970 + 120,
              activity_window_seconds == 14400, activity_buckets.count == 24,
              activity_buckets.allSatisfy({ $0.isFinite && $0 >= 0 && $0 <= 1 }),
              daily_tracking.used_since_start_pct_points.isFinite,
              daily_tracking.used_since_start_pct_points >= 0,
              !weekly.valid || (weekly.left_pct.map { $0.isFinite && $0 >= 0 && $0 <= 100 } ?? false) else {
            throw CloudSyncError.invalidResponse
        }
        return self
    }
    func forDisplay(now: Date, offline: Bool) -> Self {
        var s = self
        s.stale = stale || offline || now.timeIntervalSince1970 - (weekly.observed_at ?? 0) > 600
        let day = Date(timeIntervalSince1970: floor(now.timeIntervalSince1970 / 86400) * 86400)
        let dayKey = String(ISO8601DateFormatter().string(from: day).prefix(10))
        if daily_tracking.day_key != dayKey {
            s.daily_tracking.day_start_left_pct = -1
            s.daily_tracking.quality = "missing"
        }
        return s
    }
}

enum CloudSyncError: Error, LocalizedError {
    case invalidConfiguration, accountMismatch, invalidResponse, timeout, http(Int), noSnapshot
    var errorDescription: String? {
        switch self {
        case .invalidConfiguration: return "Cloud sync configuration invalid"
        case .accountMismatch: return "Cloud sync account differs from Codex login"
        case .invalidResponse: return "Cloud sync response invalid"
        case .timeout: return "Cloud sync timed out"
        case .http(let status): return "Cloud sync HTTP \(status)"
        case .noSnapshot: return "Cloud sync waiting for first quota sample"
        }
    }
}

// Runs only on the Bridge utility queue. The persisted quota outbox is removed
// only after a valid acknowledged response. Activity uses stable IDs derived
// from local history, so retransmitting checkpoints is safe.
final class CloudUsageSynchronizer {
    static let shared = CloudUsageSynchronizer(directory: FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/M5StopWatch/StopWatchBleBridge"))
    private struct State: Codable {
        var account_scope: String
        var observations: [CloudQuotaObservation] = []
        var snapshot: CloudUsageSnapshot?
    }
    private struct Batch: Encodable {
        let version = 1
        var account_scope: String; var device_id: String
        var observations: [CloudQuotaObservation]; var activity: [CloudActivityEvent]
    }
    private struct Envelope: Decodable { var ok: Bool; var shared_usage: CloudUsageSnapshot }
    private let directory: URL
    private let transport: (URLRequest) throws -> Data
    private var lastQuotaAttempt = Date.distantPast
    private var lastErrorText: String?
    var lastError: String? { lastErrorText }
    var configured: Bool { FileManager.default.fileExists(atPath: directory.appendingPathComponent("cloud-sync.json").path) }

    init(directory: URL, transport: @escaping (URLRequest) throws -> Data = CloudUsageSynchronizer.request) {
        self.directory = directory; self.transport = transport
    }
    func refresh(accountID: String, activity: [CloudActivityEvent], quotaInterval: TimeInterval = 300,
                 now: Date = Date(), sample: () throws -> CloudQuotaObservation) throws -> CloudUsageSnapshot {
        let c = try JSONDecoder().decode(CloudSyncConfiguration.self, from: Data(contentsOf: directory.appendingPathComponent("cloud-sync.json")))
        guard let url = URL(string: c.endpoint), url.scheme == "https", url.host != nil,
              url.user == nil, url.password == nil, c.token.count >= 32, !c.device_id.isEmpty else {
            throw CloudSyncError.invalidConfiguration
        }
        guard !accountID.isEmpty, c.account_scope == codexSyncAccountScope(accountID) else { throw CloudSyncError.accountMismatch }
        let stateURL = directory.appendingPathComponent("cloud-sync-state.json")
        var state: State
        if FileManager.default.fileExists(atPath: stateURL.path) {
            // A corrupt outbox is not silently discarded.
            state = try JSONDecoder().decode(State.self, from: Data(contentsOf: stateURL))
            guard state.account_scope == c.account_scope else { throw CloudSyncError.accountMismatch }
        } else { state = State(account_scope: c.account_scope, observations: legacyObservations(now: now)) }
        state.observations.removeAll { $0.at < now.timeIntervalSince1970 - 7 * 86400 }
        let crossedBoundary = floor(lastQuotaAttempt.timeIntervalSince1970 / 86400) != floor(now.timeIntervalSince1970 / 86400)
        lastErrorText = nil
        if now.timeIntervalSince(lastQuotaAttempt) >= max(60, quotaInterval) || crossedBoundary {
            lastQuotaAttempt = now
            do {
                let o = try sample()
                guard o.left.isFinite, o.left >= 0, o.left <= 100 else { throw CloudSyncError.invalidResponse }
                state.observations.append(o)
            } catch { lastErrorText = "Quota collection unavailable" }
        }
        try save(state, to: stateURL)
        let batch = Batch(account_scope: c.account_scope, device_id: c.device_id,
                          observations: Array(state.observations.prefix(2500)), activity: Array(activity.suffix(3500)))
        var request = URLRequest(url: url, timeoutInterval: 8)
        request.httpMethod = "POST"
        request.setValue("Bearer \(c.token)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONEncoder().encode(["shared_usage": batch])
        do {
            let envelope = try JSONDecoder().decode(Envelope.self, from: transport(request))
            guard envelope.ok else { throw CloudSyncError.invalidResponse }
            let snapshot = try envelope.shared_usage.validated(now: now)
            guard snapshot.as_of >= (state.snapshot?.as_of ?? 0) else { throw CloudSyncError.invalidResponse }
            let sent = Set(batch.observations.map(\.id))
            state.observations.removeAll { sent.contains($0.id) }
            state.snapshot = snapshot
            try save(state, to: stateURL)
            return snapshot.forDisplay(now: now, offline: false)
        } catch {
            lastErrorText = "Cloud offline; pending statistics retained"
            guard let snapshot = state.snapshot else { throw error }
            return try snapshot.validated(now: now).forDisplay(now: now, offline: true)
        }
    }
    private func save(_ state: State, to url: URL) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try JSONEncoder().encode(state).write(to: url, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }
    private func legacyObservations(now: Date) -> [CloudQuotaObservation] {
        // Preserve the old observed baseline, but explicitly label it estimated:
        // the old tracker did NOT prove it sampled at exactly 08:00.
        let path = directory.appendingPathComponent("codex-weekly-daily.json")
        guard let data = try? Data(contentsOf: path),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let startText = obj["periodStart"] as? String,
              let endText = obj["updatedAt"] as? String,
              let start = ISO8601DateFormatter().date(from: startText),
              let end = ISO8601DateFormatter().date(from: endText),
              let baseline = obj["dayStartLeftPct"] as? Double,
              let left = obj["previousLeftPct"] as? Double,
              obj["resetCount"] as? Int == 0,
              baseline >= left, baseline <= 100, left >= 0,
              start.timeIntervalSince1970.truncatingRemainder(dividingBy: 86400) == 0,
              end >= start, end <= now, now.timeIntervalSince(start) < 7 * 86400 else { return [] }
        return [CloudQuotaObservation(id: "legacy-baseline-\(Int(start.timeIntervalSince1970))", at: start.timeIntervalSince1970, left: baseline, reset_at: nil, partial: true),
                CloudQuotaObservation(id: "legacy-latest-\(Int(end.timeIntervalSince1970))", at: end.timeIntervalSince1970, left: left, reset_at: nil, partial: true)]
    }
    private static func request(_ request: URLRequest) throws -> Data {
        final class ResultBox: @unchecked Sendable {
            let lock = NSLock()
            var value: Result<Data, Error>?
        }
        let box = ResultBox(), semaphore = DispatchSemaphore(value: 0)
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 8
        configuration.timeoutIntervalForResource = 10
        final class NoRedirect: NSObject, URLSessionTaskDelegate {
            func urlSession(_ session: URLSession, task: URLSessionTask,
                            willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
                            completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
        }
        let session = URLSession(configuration: configuration, delegate: NoRedirect(), delegateQueue: nil)
        defer { session.invalidateAndCancel() }
        let task = session.dataTask(with: request) { data, response, error in
            box.lock.lock()
            defer { box.lock.unlock(); semaphore.signal() }
            if let error { box.value = .failure(error) }
            else if let http = response as? HTTPURLResponse, http.statusCode != 200 { box.value = .failure(CloudSyncError.http(http.statusCode)) }
            else if let data, data.count < 128 * 1024 { box.value = .success(data) }
            else { box.value = .failure(CloudSyncError.invalidResponse) }
        }
        task.resume()
        guard semaphore.wait(timeout: .now() + 11) == .success else { task.cancel(); throw CloudSyncError.timeout }
        box.lock.lock(); defer { box.lock.unlock() }
        return try box.value!.get()
    }
}
