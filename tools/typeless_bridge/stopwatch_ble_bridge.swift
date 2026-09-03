import AppKit
import ApplicationServices
import Carbon
import CoreBluetooth
import Darwin
import Foundation
import IOKit

private let deviceNamePrefix = "M5Codex-"
private let legacyDeviceNames: Set<String> = ["M5Codex-HID4", "M5Codex-HID5"]
private let hidServiceUUID = CBUUID(string: "1812")
private let hidReportUUID = CBUUID(string: "2A4D")
private let serviceUUID = CBUUID(string: "ABCD0000-E819-B394-6344-2A2F31424C45")
private let eventUUID = CBUUID(string: "ABCD0001-E819-B394-6344-2A2F31424C45")
private let statusUUID = CBUUID(string: "ABCD0002-E819-B394-6344-2A2F31424C45")
private let panelUUID = CBUUID(string: "ABCD0003-E819-B394-6344-2A2F31424C45")
private let healthCheckInterval: TimeInterval = 5
private let microphoneOutputPreflightIdleSeconds: TimeInterval = 8
private let nativeHIDBridgeSettleSeconds: TimeInterval = 0.7
private let authenticationRetrySeconds: TimeInterval = 1.2
private let authenticationRetryLimit = 3
private let codexHIDVendorID = 0x303A
private let codexHIDProductID = 0x8360
private let codexDeviceId = "m5stack-stopwatch"
private let supportDirectoryURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Application Support/M5StopWatch/StopWatchBleBridge")
private let configFileURL = supportDirectoryURL.appendingPathComponent("config.json")
private let quotaDailyStateFileURL = supportDirectoryURL.appendingPathComponent("codex-weekly-daily.json")
private let activityHistoryFileURL = supportDirectoryURL.appendingPathComponent("activity-history.json")
private let codexGlobalStateFileURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent(".codex/.codex-global-state.json")
private let codexStateDatabaseURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent(".codex/state_5.sqlite")
private let logFileURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Logs/stopwatch-ble-bridge.log")
private let bridgeSettingsChangedNotification = Notification.Name("StopWatchBleBridgeSettingsChanged")
private let typelessPrimaryDownDebounceSeconds: TimeInterval = 0.35
private let typelessLaunchTimeoutSeconds: TimeInterval = 6
private let typelessShortcutRegistrationDelaySeconds: TimeInterval = 0.65
private let typelessStartIdleGraceSeconds: TimeInterval = 1.0
private let typelessProcessingIdleGraceSeconds: TimeInterval = 1.2
private let typelessProcessingMaximumSeconds: TimeInterval = 2.8
private let microphoneStreamFaultSeconds: TimeInterval = 1.2
private let microphoneStreamStartGraceSeconds: TimeInterval = 1.5
private let microphoneFatalPacketGap: UInt64 = 25
private let microphoneFatalDroppedFrames: UInt32 = 25

private func isSafeCodexThreadID(_ id: String) -> Bool {
    guard id.count == 36 else { return false }
    let allowed = CharacterSet(charactersIn: "0123456789abcdefABCDEF-")
    return id.rangeOfCharacter(from: allowed.inverted) == nil
}

private func effectiveLocalCodexUnreadCount(_ ids: Set<String>) -> Int {
    let safeIDs = ids.filter(isSafeCodexThreadID)
    guard !safeIDs.isEmpty else { return 0 }
    guard FileManager.default.fileExists(atPath: codexStateDatabaseURL.path) else {
        return safeIDs.count
    }

    let quoted = safeIDs.sorted().map { "'\($0)'" }.joined(separator: ",")
    let query = "SELECT id, archived FROM threads WHERE id IN (\(quoted));"
    let process = Process()
    let output = Pipe()
    process.executableURL = URL(fileURLWithPath: "/usr/bin/sqlite3")
    process.arguments = ["-separator", "\t", codexStateDatabaseURL.path, query]
    process.standardOutput = output
    process.standardError = Pipe()

    do {
        try process.run()
        let data = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0,
              let text = String(data: data, encoding: .utf8) else {
            return safeIDs.count
        }

        var knownIDs = Set<String>()
        var activeCount = 0
        for line in text.split(separator: "\n") {
            let fields = line.split(separator: "\t", omittingEmptySubsequences: false)
            guard fields.count == 2 else { continue }
            let id = String(fields[0])
            knownIDs.insert(id)
            if fields[1] == "0" {
                activeCount += 1
            }
        }
        // A brand-new completion can reach the UI state just before its SQLite
        // row is visible. Treat an unknown ID as unread for that short window.
        return activeCount + safeIDs.subtracting(knownIDs).count
    } catch {
        return safeIDs.count
    }
}

private func currentCodexUnreadTaskCount() -> Int? {
    guard let data = try? Data(contentsOf: codexGlobalStateFileURL),
          let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let persisted = root["electron-persisted-atom-state"] as? [String: Any],
          let unreadByHost = persisted["unread-thread-ids-by-host-v1"] as? [String: Any] else {
        return nil
    }

    var localIDs = Set<String>()
    var remoteIDs = Set<String>()
    for (host, value) in unreadByHost {
        guard let ids = value as? [String] else { continue }
        if host == "local" {
            localIDs.formUnion(ids)
        } else {
            remoteIDs.formUnion(ids)
        }
    }
    return effectiveLocalCodexUnreadCount(localIDs) + remoteIDs.count
}

private struct CodexUnreadTask: Equatable {
    let id: String
    let title: String
}

private func currentCodexUnreadTasks(limit: Int = 6) -> [CodexUnreadTask]? {
    guard let data = try? Data(contentsOf: codexGlobalStateFileURL),
          let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let persisted = root["electron-persisted-atom-state"] as? [String: Any],
          let unreadByHost = persisted["unread-thread-ids-by-host-v1"] as? [String: Any] else {
        return nil
    }

    var localIDs = Set<String>()
    var remoteIDs = Set<String>()
    for (host, value) in unreadByHost {
        guard let ids = value as? [String] else { continue }
        if host == "local" {
            localIDs.formUnion(ids.filter(isSafeCodexThreadID))
        } else {
            remoteIDs.formUnion(ids.filter(isSafeCodexThreadID))
        }
    }

    var tasks: [CodexUnreadTask] = []
    var knownLocalIDs = Set<String>()
    if !localIDs.isEmpty, FileManager.default.fileExists(atPath: codexStateDatabaseURL.path) {
        let quoted = localIDs.sorted().map { "'\($0)'" }.joined(separator: ",")
        let title = "replace(replace(COALESCE(NULLIF(name,''),NULLIF(title,''),'Unread Codex task'),char(9),' '),char(10),' ')"
        let query = "SELECT id, \(title), archived FROM threads WHERE id IN (\(quoted)) ORDER BY updated_at DESC;"
        let process = Process()
        let output = Pipe()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/sqlite3")
        process.arguments = ["-separator", "\t", codexStateDatabaseURL.path, query]
        process.standardOutput = output
        process.standardError = Pipe()
        do {
            try process.run()
            let outputData = output.fileHandleForReading.readDataToEndOfFile()
            process.waitUntilExit()
            if process.terminationStatus == 0,
               let text = String(data: outputData, encoding: .utf8) {
                for line in text.split(separator: "\n") {
                    let fields = line.split(separator: "\t", maxSplits: 2, omittingEmptySubsequences: false)
                    guard fields.count == 3 else { continue }
                    let id = String(fields[0])
                    guard isSafeCodexThreadID(id) else { continue }
                    knownLocalIDs.insert(id)
                    guard fields[2] == "0", tasks.count < limit else { continue }
                    let taskTitle = String(fields[1]).trimmingCharacters(in: .whitespacesAndNewlines)
                    tasks.append(CodexUnreadTask(id: id,
                                                 title: taskTitle.isEmpty ? "Unread Codex task" : taskTitle))
                }
            }
        } catch {
            // The count-only status remains available if SQLite is briefly busy.
        }
    }

    for id in localIDs.subtracting(knownLocalIDs).sorted() where tasks.count < limit {
        tasks.append(CodexUnreadTask(id: id, title: "Unread Codex task"))
    }
    for id in remoteIDs.sorted() where tasks.count < limit {
        tasks.append(CodexUnreadTask(id: id, title: "Remote Codex task"))
    }
    return Array(tasks.prefix(max(limit, 0)))
}

private func currentCodexThinkingEffort() -> String {
    guard let data = try? Data(contentsOf: codexGlobalStateFileURL),
          let root = try? JSONSerialization.jsonObject(with: data) else {
        return "--"
    }

    func findEffort(_ value: Any) -> String? {
        if let object = value as? [String: Any] {
            if let effort = object["thinkingEffort"] as? String, !effort.isEmpty {
                return effort
            }
            for nested in object.values {
                if let effort = findEffort(nested) {
                    return effort
                }
            }
        } else if let array = value as? [Any] {
            for nested in array {
                if let effort = findEffort(nested) {
                    return effort
                }
            }
        }
        return nil
    }
    return findEffort(root) ?? "--"
}

private enum MicrophoneControlCommand: UInt8 {
    case disable = 0
    case continuous = 1
    case armOnDemand = 2
    case startVoice = 3
    case stopVoice = 4
}

private func isSupportedDeviceName(_ name: String?) -> Bool {
    guard let name else { return false }
    return name.hasPrefix(deviceNamePrefix) || legacyDeviceNames.contains(name)
}

private func ioRegistryProperty(_ service: io_registry_entry_t, _ key: String) -> Any? {
    IORegistryEntryCreateCFProperty(service, key as CFString, kCFAllocatorDefault, 0)?
        .takeRetainedValue()
}

private func isNativeCodexHIDConnected() -> Bool {
    guard let matching = IOServiceMatching("IOHIDDevice") else { return false }
    var iterator: io_iterator_t = 0
    guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == KERN_SUCCESS else {
        return false
    }
    defer { IOObjectRelease(iterator) }

    while true {
        let service = IOIteratorNext(iterator)
        guard service != 0 else { break }
        let vendor = (ioRegistryProperty(service, "VendorID") as? NSNumber)?.intValue
        let product = (ioRegistryProperty(service, "ProductID") as? NSNumber)?.intValue
        let name = ioRegistryProperty(service, "Product") as? String
        let matched = vendor == codexHIDVendorID
            && product == codexHIDProductID
            && isSupportedDeviceName(name)
        IOObjectRelease(service)
        if matched { return true }
    }
    return false
}

private struct KeyBinding: Codable, Equatable {
    var name: String
    var macKeyCode: UInt16
    var hidKeyCode: UInt8
}

private let availableKeyBindings: [KeyBinding] = [
    KeyBinding(name: "F13", macKeyCode: UInt16(kVK_F13), hidKeyCode: 0x68),
    KeyBinding(name: "F14", macKeyCode: UInt16(kVK_F14), hidKeyCode: 0x69),
    KeyBinding(name: "F15", macKeyCode: UInt16(kVK_F15), hidKeyCode: 0x6A),
    KeyBinding(name: "F16", macKeyCode: UInt16(kVK_F16), hidKeyCode: 0x6B),
    KeyBinding(name: "F17", macKeyCode: UInt16(kVK_F17), hidKeyCode: 0x6C),
    KeyBinding(name: "F18", macKeyCode: UInt16(kVK_F18), hidKeyCode: 0x6D),
    KeyBinding(name: "F19", macKeyCode: UInt16(kVK_F19), hidKeyCode: 0x6E),
    KeyBinding(name: "F20", macKeyCode: UInt16(kVK_F20), hidKeyCode: 0x6F),
    KeyBinding(name: "Return", macKeyCode: UInt16(kVK_Return), hidKeyCode: 0x28),
    KeyBinding(name: "Space", macKeyCode: UInt16(kVK_Space), hidKeyCode: 0x2C),
    KeyBinding(name: "Tab", macKeyCode: UInt16(kVK_Tab), hidKeyCode: 0x2B),
    KeyBinding(name: "Escape", macKeyCode: UInt16(kVK_Escape), hidKeyCode: 0x29),
    KeyBinding(name: "Right Option", macKeyCode: UInt16(kVK_RightOption), hidKeyCode: 0xE6),
]

private let availableShakeActions = ["Clear Input", "Command+Return", "Escape", "Return", "None"]
private let availableInputModes = ["Typeless", "WeChat IME"]

private let inputModeTitles: [InputMode: String] = [
    .typeless: "Typeless 听写",
    .wechatIME: "微信输入法"
]

private let inputModeDescriptions: [InputMode: String] = [
    .typeless: "A 键触发 Typeless 开始/停止听写，B 键确认或回车。",
    .wechatIME: "A 键按住右 Option 进行输入，松开结束；B 键确认。"
]

private let shakeActionTitles: [String: String] = [
    "Clear Input": "清空输入",
    "Command+Return": "Codex 引导（⌘↩）",
    "Escape": "Esc",
    "Return": "回车",
    "None": "不处理"
]

private enum InputMode: String, Codable, Equatable {
    case typeless = "Typeless"
    case wechatIME = "WeChat IME"
}

private func keyBinding(named name: String, fallback fallbackName: String, custom: [KeyBinding] = []) -> KeyBinding {
    custom.first { $0.name == name } ??
        availableKeyBindings.first { $0.name == name } ??
        custom.first { $0.name == fallbackName } ??
        availableKeyBindings.first { $0.name == fallbackName } ??
        availableKeyBindings[0]
}

private func isKnownKeyBindingName(_ name: String, custom: [KeyBinding]) -> Bool {
    availableKeyBindings.contains { $0.name == name } || custom.contains { $0.name == name }
}

private func hidUsageForMacKeyCode(_ keyCode: UInt16) -> UInt8? {
    switch Int(keyCode) {
    case kVK_ANSI_A: return 0x04
    case kVK_ANSI_B: return 0x05
    case kVK_ANSI_C: return 0x06
    case kVK_ANSI_D: return 0x07
    case kVK_ANSI_E: return 0x08
    case kVK_ANSI_F: return 0x09
    case kVK_ANSI_G: return 0x0A
    case kVK_ANSI_H: return 0x0B
    case kVK_ANSI_I: return 0x0C
    case kVK_ANSI_J: return 0x0D
    case kVK_ANSI_K: return 0x0E
    case kVK_ANSI_L: return 0x0F
    case kVK_ANSI_M: return 0x10
    case kVK_ANSI_N: return 0x11
    case kVK_ANSI_O: return 0x12
    case kVK_ANSI_P: return 0x13
    case kVK_ANSI_Q: return 0x14
    case kVK_ANSI_R: return 0x15
    case kVK_ANSI_S: return 0x16
    case kVK_ANSI_T: return 0x17
    case kVK_ANSI_U: return 0x18
    case kVK_ANSI_V: return 0x19
    case kVK_ANSI_W: return 0x1A
    case kVK_ANSI_X: return 0x1B
    case kVK_ANSI_Y: return 0x1C
    case kVK_ANSI_Z: return 0x1D
    case kVK_ANSI_1: return 0x1E
    case kVK_ANSI_2: return 0x1F
    case kVK_ANSI_3: return 0x20
    case kVK_ANSI_4: return 0x21
    case kVK_ANSI_5: return 0x22
    case kVK_ANSI_6: return 0x23
    case kVK_ANSI_7: return 0x24
    case kVK_ANSI_8: return 0x25
    case kVK_ANSI_9: return 0x26
    case kVK_ANSI_0: return 0x27
    case kVK_Return: return 0x28
    case kVK_Escape: return 0x29
    case kVK_Delete: return 0x2A
    case kVK_Tab: return 0x2B
    case kVK_Space: return 0x2C
    case kVK_ANSI_Minus: return 0x2D
    case kVK_ANSI_Equal: return 0x2E
    case kVK_ANSI_LeftBracket: return 0x2F
    case kVK_ANSI_RightBracket: return 0x30
    case kVK_ANSI_Backslash: return 0x31
    case kVK_ANSI_Semicolon: return 0x33
    case kVK_ANSI_Quote: return 0x34
    case kVK_ANSI_Grave: return 0x35
    case kVK_ANSI_Comma: return 0x36
    case kVK_ANSI_Period: return 0x37
    case kVK_ANSI_Slash: return 0x38
    case kVK_F1: return 0x3A
    case kVK_F2: return 0x3B
    case kVK_F3: return 0x3C
    case kVK_F4: return 0x3D
    case kVK_F5: return 0x3E
    case kVK_F6: return 0x3F
    case kVK_F7: return 0x40
    case kVK_F8: return 0x41
    case kVK_F9: return 0x42
    case kVK_F10: return 0x43
    case kVK_F11: return 0x44
    case kVK_F12: return 0x45
    default: return nil
    }
}

private func capturedKeyBinding(from event: NSEvent) -> KeyBinding? {
    let keyCode = UInt16(event.keyCode)
    if let existing = availableKeyBindings.first(where: { $0.macKeyCode == keyCode }) {
        return existing
    }
    guard let hidKeyCode = hidUsageForMacKeyCode(keyCode) else {
        return nil
    }
    let raw = (event.charactersIgnoringModifiers ?? "").uppercased()
    let name = raw.isEmpty ? "Key \(keyCode)" : raw
    return KeyBinding(name: name, macKeyCode: keyCode, hidKeyCode: hidKeyCode)
}

private struct BridgeSettings: Codable, Equatable {
    var leftKeyName: String = "F19"
    var rightKeyName: String = "Return"
    var typelessLeftKeyName: String = "F19"
    var typelessRightKeyName: String = "Return"
    var wechatLeftKeyName: String = "Right Option"
    var wechatRightKeyName: String = "Return"
    var confirmLongActionName: String = "Command+Return"
    var shakeActionName: String = "Clear Input"
    var customKeyBindings: [KeyBinding] = []
    var inputModeName: String = InputMode.typeless.rawValue
    var quotaRefreshSeconds: Int = 300
    var enableCodexQuota: Bool = true
    var syncTypelessShortcut: Bool = false
    var virtualMicrophoneEnabled: Bool = false

    private enum CodingKeys: String, CodingKey {
        case leftKeyName
        case rightKeyName
        case typelessLeftKeyName
        case typelessRightKeyName
        case wechatLeftKeyName
        case wechatRightKeyName
        case confirmLongActionName
        case shakeActionName
        case customKeyBindings
        case inputModeName
        case quotaRefreshSeconds
        case enableCodexQuota
        case syncTypelessShortcut
        case virtualMicrophoneEnabled
    }

    init() {}

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        leftKeyName = try container.decodeIfPresent(String.self, forKey: .leftKeyName) ?? "F19"
        rightKeyName = try container.decodeIfPresent(String.self, forKey: .rightKeyName) ?? "Return"
        typelessLeftKeyName = try container.decodeIfPresent(String.self, forKey: .typelessLeftKeyName) ?? leftKeyName
        typelessRightKeyName = try container.decodeIfPresent(String.self, forKey: .typelessRightKeyName) ?? rightKeyName
        wechatLeftKeyName = try container.decodeIfPresent(String.self, forKey: .wechatLeftKeyName) ?? "Right Option"
        wechatRightKeyName = try container.decodeIfPresent(String.self, forKey: .wechatRightKeyName) ?? "Return"
        confirmLongActionName = try container.decodeIfPresent(String.self, forKey: .confirmLongActionName) ?? "Command+Return"
        shakeActionName = try container.decodeIfPresent(String.self, forKey: .shakeActionName) ?? "Clear Input"
        customKeyBindings = try container.decodeIfPresent([KeyBinding].self, forKey: .customKeyBindings) ?? []
        inputModeName = try container.decodeIfPresent(String.self, forKey: .inputModeName) ?? InputMode.typeless.rawValue
        quotaRefreshSeconds = try container.decodeIfPresent(Int.self, forKey: .quotaRefreshSeconds) ?? 300
        enableCodexQuota = try container.decodeIfPresent(Bool.self, forKey: .enableCodexQuota) ?? true
        syncTypelessShortcut = try container.decodeIfPresent(Bool.self, forKey: .syncTypelessShortcut) ?? false
        virtualMicrophoneEnabled = try container.decodeIfPresent(Bool.self, forKey: .virtualMicrophoneEnabled) ?? false
    }

    var leftKey: KeyBinding {
        switch inputMode {
        case .typeless:
            return keyBinding(named: typelessLeftKeyName, fallback: "F19", custom: customKeyBindings)
        case .wechatIME:
            return keyBinding(named: wechatLeftKeyName, fallback: "Right Option", custom: customKeyBindings)
        }
    }

    var rightKey: KeyBinding {
        switch inputMode {
        case .typeless:
            return keyBinding(named: typelessRightKeyName, fallback: "Return", custom: customKeyBindings)
        case .wechatIME:
            return keyBinding(named: wechatRightKeyName, fallback: "Return", custom: customKeyBindings)
        }
    }

    var shakeKey: KeyBinding? {
        guard shakeActionName.hasPrefix("Key:") else { return nil }
        let keyName = String(shakeActionName.dropFirst(4))
        return keyBinding(named: keyName, fallback: "Escape", custom: customKeyBindings)
    }

    var confirmLongKey: KeyBinding? {
        guard confirmLongActionName.hasPrefix("Key:") else { return nil }
        let keyName = String(confirmLongActionName.dropFirst(4))
        return keyBinding(named: keyName, fallback: "Return", custom: customKeyBindings)
    }

    var inputMode: InputMode {
        InputMode(rawValue: inputModeName) ?? .typeless
    }

    var sanitized: BridgeSettings {
        var copy = self
        copy.customKeyBindings = Array(Dictionary(grouping: copy.customKeyBindings, by: \.name).compactMap { $0.value.first })
        if !isKnownKeyBindingName(copy.leftKeyName, custom: copy.customKeyBindings) {
            copy.leftKeyName = "F19"
        }
        if !isKnownKeyBindingName(copy.rightKeyName, custom: copy.customKeyBindings) {
            copy.rightKeyName = "Return"
        }
        if !isKnownKeyBindingName(copy.typelessLeftKeyName, custom: copy.customKeyBindings) {
            copy.typelessLeftKeyName = copy.leftKeyName
        }
        if !isKnownKeyBindingName(copy.typelessRightKeyName, custom: copy.customKeyBindings) {
            copy.typelessRightKeyName = copy.rightKeyName
        }
        if !isKnownKeyBindingName(copy.wechatLeftKeyName, custom: copy.customKeyBindings) {
            copy.wechatLeftKeyName = "Right Option"
        }
        if !isKnownKeyBindingName(copy.wechatRightKeyName, custom: copy.customKeyBindings) {
            copy.wechatRightKeyName = "Return"
        }
        if copy.confirmLongActionName.hasPrefix("Key:") {
            let keyName = String(copy.confirmLongActionName.dropFirst(4))
            if !isKnownKeyBindingName(keyName, custom: copy.customKeyBindings) {
                copy.confirmLongActionName = "Command+Return"
            }
        } else if !availableShakeActions.contains(copy.confirmLongActionName) {
            copy.confirmLongActionName = "Command+Return"
        }
        if copy.shakeActionName.hasPrefix("Key:") {
            let keyName = String(copy.shakeActionName.dropFirst(4))
            if !isKnownKeyBindingName(keyName, custom: copy.customKeyBindings) {
                copy.shakeActionName = "Clear Input"
            }
        } else if !availableShakeActions.contains(copy.shakeActionName) {
            copy.shakeActionName = "Clear Input"
        }
        if !availableInputModes.contains(copy.inputModeName) {
            copy.inputModeName = InputMode.typeless.rawValue
        }
        copy.quotaRefreshSeconds = min(3600, max(60, copy.quotaRefreshSeconds))
        switch copy.inputMode {
        case .typeless:
            copy.leftKeyName = copy.typelessLeftKeyName
            copy.rightKeyName = copy.typelessRightKeyName
        case .wechatIME:
            copy.leftKeyName = copy.wechatLeftKeyName
            copy.rightKeyName = copy.wechatRightKeyName
        }
        return copy
    }
}

private final class SettingsStore {
    static let shared = SettingsStore()
    private(set) var settings = BridgeSettings()

    private init() {
        settings = load()
    }

    func reload() {
        settings = load()
    }

    func save(_ next: BridgeSettings) {
        settings = next.sanitized
        do {
            try FileManager.default.createDirectory(at: supportDirectoryURL, withIntermediateDirectories: true)
            let data = try JSONEncoder().encode(settings)
            try data.write(to: configFileURL, options: .atomic)
        } catch {
            log("settings save failed: \(error.localizedDescription)")
        }
    }

    private func load() -> BridgeSettings {
        do {
            let data = try Data(contentsOf: configFileURL)
            return try JSONDecoder().decode(BridgeSettings.self, from: data).sanitized
        } catch {
            return BridgeSettings()
        }
    }
}

private final class BridgeStatusCenter {
    static let shared = BridgeStatusCenter()

    var bleStatus = "Starting" { didSet { refreshMenu() } }
    var selfCheckStatus = "Checking" { didSet { refreshMenu() } }
    var typelessStatus = "Unknown" { didSet { refreshMenu() } }
    var inputMode = SettingsStore.shared.settings.inputMode.rawValue { didSet { refreshMenu() } }
    var quotaStatus = "Waiting" { didSet { refreshMenu() } }
    var microphoneStatus = "Off" { didSet { refreshMenu() } }
    var lastError = "" { didSet { refreshMenu() } }

    weak var appDelegate: BridgeAppDelegate?

    func refreshMenu() {
        DispatchQueue.main.async { [weak self] in
            self?.appDelegate?.refreshMenu()
        }
    }
}

private final class OpenWatcherHomeActivityTracker {
    static let shared = OpenWatcherHomeActivityTracker()

    private static let activityWindowSeconds: TimeInterval = 4 * 60 * 60
    private static let recordingStartTargetPerBucket = 4.0

    private struct RecordingInterval: Codable {
        var start: Date
        var end: Date
    }

    private struct InteractionEvent: Codable {
        var date: Date
        var units: Double
    }

    private struct StoredHistory: Codable {
        var intervals: [RecordingInterval]
        var interactions: [InteractionEvent]
    }

    private var intervals: [RecordingInterval] = []
    private var interactions: [InteractionEvent] = []
    private var recordingStartedAt: Date?
    private var currentPhase = "idle"
    private let lock = NSLock()

    private init() {
        loadHistory()
    }

    func record(_ kind: String, weight: Double = 1.0) {
        let now = Date()
        lock.lock()
        var changed = false

        if kind == "recording" {
            if currentPhase != "recording" {
                recordingStartedAt = now
                currentPhase = "recording"
                changed = true
            }
        } else if kind == "processing" || kind == "idle" || kind == "error" {
            if currentPhase == "recording", let start = recordingStartedAt, now > start {
                intervals.append(RecordingInterval(start: start, end: now))
                changed = true
            }
            recordingStartedAt = nil
            currentPhase = kind
        } else {
            let units: Double
            switch kind {
            case "primary_down": units = 0.25
            case "enter", "shake": units = 0.15
            default: units = 0.0
            }
            if units > 0 {
                interactions.append(InteractionEvent(date: now,
                                                     units: units * max(0.05, min(1.0, weight))))
                changed = true
            }
        }

        pruneLocked(now: now)
        let history = changed ? StoredHistory(intervals: intervals, interactions: interactions) : nil
        lock.unlock()

        if let history {
            saveHistory(history)
        }
    }

    func buckets(count: Int = 24, windowSeconds: TimeInterval = activityWindowSeconds) -> [Double] {
        let now = Date()
        let bucketSeconds = windowSeconds / Double(count)
        var result = Array(repeating: 0.0, count: count)

        lock.lock()
        var intervalSnapshot = intervals
        if let start = recordingStartedAt, now > start {
            intervalSnapshot.append(RecordingInterval(start: start, end: now))
        }
        let interactionSnapshot = interactions
        lock.unlock()

        let windowStart = now.addingTimeInterval(-windowSeconds)
        for index in 0..<count {
            let bucketStart = windowStart.addingTimeInterval(Double(index) * bucketSeconds)
            let bucketEnd = bucketStart.addingTimeInterval(bucketSeconds)
            var recordingSeconds = 0.0
            var recordingStarts = 0.0

            for interval in intervalSnapshot {
                let overlapStart = max(interval.start, bucketStart)
                let overlapEnd = min(interval.end, bucketEnd)
                if overlapEnd > overlapStart {
                    recordingSeconds += overlapEnd.timeIntervalSince(overlapStart)
                }
                if interval.start >= bucketStart && interval.start < bucketEnd {
                    recordingStarts += 1.0
                }
            }

            var interactionUnits = 0.0
            for event in interactionSnapshot where event.date >= bucketStart && event.date < bucketEnd {
                interactionUnits += event.units
            }

            let durationScore = min(1.0, recordingSeconds / bucketSeconds)
            let frequencyScore = min(1.0,
                                     (recordingStarts + interactionUnits) /
                                         Self.recordingStartTargetPerBucket)
            result[index] = min(1.0, durationScore * 0.70 + frequencyScore * 0.30)
        }
        return result.map { round($0 * 100) / 100 }
    }

    func cloudEvents() -> [CloudActivityEvent] {
        let now = Date()
        lock.lock()
        defer { lock.unlock() }
        pruneLocked(now: now)
        var events = intervals.map { CloudActivityEvent.make(kind: "recording", start: $0.start, end: $0.end) }
        if let start = recordingStartedAt, now > start {
            events.append(.make(kind: "recording", start: start, end: now))
        }
        events += interactions.map { .make(kind: "interaction", start: $0.date, end: $0.date, units: $0.units) }
        return events.sorted { $0.start < $1.start }
    }

    private func pruneLocked(now: Date) {
        let cutoff = now.addingTimeInterval(-Self.activityWindowSeconds)
        intervals.removeAll { $0.end < cutoff }
        interactions.removeAll { $0.date < cutoff }
    }

    private func loadHistory() {
        guard let data = try? Data(contentsOf: activityHistoryFileURL),
              let history = try? JSONDecoder().decode(StoredHistory.self, from: data) else {
            return
        }
        intervals = history.intervals
        interactions = history.interactions
        pruneLocked(now: Date())
    }

    private func saveHistory(_ history: StoredHistory) {
        do {
            try FileManager.default.createDirectory(at: supportDirectoryURL,
                                                    withIntermediateDirectories: true)
            let data = try JSONEncoder().encode(history)
            try data.write(to: activityHistoryFileURL, options: .atomic)
        } catch {
            log("activity history save failed: \(error.localizedDescription)")
        }
    }
}

private struct BridgeSelfCheck {
    var summary: String
    var accessibility: String
    var typeless: String
    var quota: String
    var issues: [String]
}

private func typelessInstallStatus() -> String {
    if runningTypelessApplication() != nil {
        return "Running"
    }
    return typelessApplicationURL() != nil ? "Installed" : "Optional"
}

private func typelessApplicationURL() -> URL? {
    let appURLs = [
        URL(fileURLWithPath: "/Applications/Typeless.app"),
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications/Typeless.app")
    ]
    return appURLs.first(where: { FileManager.default.fileExists(atPath: $0.path) })
}

private func runningTypelessApplication() -> NSRunningApplication? {
    NSWorkspace.shared.runningApplications.first {
        $0.bundleIdentifier == "now.typeless.desktop" || $0.localizedName == "Typeless"
    }
}

private func isTypelessRunning() -> Bool {
    runningTypelessApplication() != nil
}

private func codexQuotaAuthStatus(settings: BridgeSettings) -> String {
    guard settings.enableCodexQuota else {
        return "Disabled"
    }
    let authURL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex/auth.json")
    guard FileManager.default.fileExists(atPath: authURL.path) else {
        return "Missing login"
    }
    do {
        let payload = try jsonObject(from: Data(contentsOf: authURL))
        let tokens = payload["tokens"] as? [String: Any] ?? [:]
        if let accessToken = tokens["access_token"] as? String, !accessToken.isEmpty {
            return "Ready"
        }
        return "Missing token"
    } catch {
        return "Unreadable"
    }
}

private func runBridgeSelfCheck(settings: BridgeSettings = SettingsStore.shared.settings) -> BridgeSelfCheck {
    var issues: [String] = []
    let accessibility = AXIsProcessTrusted() ? "Full access" : "Fallback only"
    if !AXIsProcessTrusted() {
        issues.append("Accessibility not authorized")
    }

    let typeless = typelessInstallStatus()
    let quota = codexQuotaAuthStatus(settings: settings)
    if settings.enableCodexQuota && quota != "Ready" {
        issues.append("Codex quota \(quota.lowercased())")
    }

    let summary = issues.isEmpty ? "Ready" : "Needs attention"
    return BridgeSelfCheck(summary: summary,
                           accessibility: accessibility,
                           typeless: typeless,
                           quota: quota,
                           issues: issues)
}

private func makeMenuBarDeviceIcon(active: Bool) -> NSImage {
    let size = NSSize(width: 24, height: 18)
    let image = NSImage(size: size)
    image.lockFocus()

    let bodyRect = NSRect(x: 4.4, y: 1.8, width: 15.2, height: 14.4)
    let bodyPath = NSBezierPath(ovalIn: bodyRect)
    (active ? NSColor.labelColor : NSColor.secondaryLabelColor).withAlphaComponent(active ? 0.92 : 0.68).setStroke()
    bodyPath.lineWidth = 1.8
    bodyPath.stroke()

    let leftButton = NSBezierPath(roundedRect: NSRect(x: 2.0, y: 6.6, width: 2.6, height: 4.8),
                                  xRadius: 1.1,
                                  yRadius: 1.1)
    let rightButton = NSBezierPath(roundedRect: NSRect(x: 19.4, y: 6.6, width: 2.6, height: 4.8),
                                   xRadius: 1.1,
                                   yRadius: 1.1)
    (active ? NSColor.systemBlue : NSColor.tertiaryLabelColor).withAlphaComponent(active ? 0.82 : 0.62).setFill()
    leftButton.fill()
    rightButton.fill()

    let screenPath = NSBezierPath(ovalIn: NSRect(x: 8.0, y: 5.4, width: 8.0, height: 7.2))
    (active ? NSColor.systemPurple : NSColor.secondaryLabelColor).withAlphaComponent(active ? 0.88 : 0.45).setFill()
    screenPath.fill()

    image.unlockFocus()
    image.isTemplate = true
    return image
}

private func log(_ message: String) {
    print(message)
    fflush(stdout)
    guard let data = "\(message)\n".data(using: .utf8) else {
        return
    }
    if FileManager.default.fileExists(atPath: logFileURL.path),
       let handle = try? FileHandle(forWritingTo: logFileURL) {
        handle.seekToEndOfFile()
        try? handle.write(contentsOf: data)
        try? handle.close()
        return
    }
    try? data.write(to: logFileURL, options: .atomic)
}

private struct VoiceState: Equatable {
    var active: Bool
    var phase: String
    var message: String

    var payload: String {
        "{\"voice_active\":\(active ? "true" : "false"),\"phase\":\"\(phase)\",\"message\":\"\(message)\"}"
    }
}

private struct FocusSnapshot {
    var processIdentifier: pid_t
    var bundleIdentifier: String?
    var appName: String
    var window: AXUIElement?
    var windowTitle: String
}

private struct InputFocusTarget {
    var focus: FocusSnapshot
    var element: AXUIElement
    var role: String
    var title: String
}

private struct Options {
    var status: String?
    var forcedState: VoiceState?
    var once = false
    var interval: TimeInterval = 1.0
    var requestAccessibility = false
    var checkAccessibility = false
}

private func requestAccessibilityAccess() -> Bool {
    let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
    return AXIsProcessTrustedWithOptions(options)
}

private func hidReportBinding(_ binding: KeyBinding) -> (modifier: UInt8, keycode: UInt8) {
    binding.name == "Right Option" ? (0x40, binding.hidKeyCode) : (0, binding.hidKeyCode)
}

private func hidActionBinding(_ action: String, customKey: KeyBinding?) -> (modifier: UInt8, keycode: UInt8) {
    if action == "Command+Return" {
        return (0x08, 0x28)
    }
    if let customKey {
        return hidReportBinding(customKey)
    }
    return (0, 0)
}

private func parseOptions() -> Options {
    var options = Options()
    var args = Array(CommandLine.arguments.dropFirst())
    while !args.isEmpty {
        let arg = args.removeFirst()
        switch arg {
        case "--status":
            if !args.isEmpty {
                options.status = args.removeFirst()
            }
        case "--active":
            options.forcedState = VoiceState(active: true, phase: "recording", message: "正在录制中")
        case "--idle":
            options.forcedState = VoiceState(active: false, phase: "idle", message: "Typeless 已停止")
        case "--interval":
            if !args.isEmpty, let value = Double(args.removeFirst()) {
                options.interval = max(0.3, value)
            }
        case "--request-accessibility":
            options.requestAccessibility = true
        case "--check-accessibility":
            options.checkAccessibility = true
        case "--once":
            options.once = true
        case "--help", "-h":
            log("""
            Usage:
              stopwatch-ble-bridge [--active|--idle|--status TEXT] [--once] [--interval SECONDS]
              stopwatch-ble-bridge --request-accessibility
              stopwatch-ble-bridge --check-accessibility

            Default mode:
              Detect Typeless state from macOS Accessibility text and write it to the StopWatch BLE bridge.

            Examples:
              tools/typeless_bridge/run_stopwatch_ble_bridge.sh --active --once
              tools/typeless_bridge/run_stopwatch_ble_bridge.sh --idle --once
              tools/typeless_bridge/run_stopwatch_ble_bridge.sh --status "正在录制中" --once
            """)
            exit(0)
        default:
            log("Unknown option: \(arg)")
            exit(2)
        }
    }
    return options
}

private func stringValue(_ value: CFTypeRef?) -> String {
    guard let value else { return "" }
    if CFGetTypeID(value) == CFStringGetTypeID() {
        return value as! String
    }
    if CFGetTypeID(value) == CFNumberGetTypeID() {
        return "\(value)"
    }
    return ""
}

private func copyAttribute(_ element: AXUIElement, _ attr: CFString) -> CFTypeRef? {
    var value: CFTypeRef?
    let err = AXUIElementCopyAttributeValue(element, attr, &value)
    return err == .success ? value : nil
}

private func boolValue(_ value: CFTypeRef?) -> Bool? {
    guard let value else { return nil }
    if CFGetTypeID(value) == CFBooleanGetTypeID() {
        return CFBooleanGetValue((value as! CFBoolean))
    }
    if CFGetTypeID(value) == CFNumberGetTypeID() {
        return (value as! NSNumber).boolValue
    }
    return nil
}

private func children(_ element: AXUIElement, limit: Int = 40) -> [AXUIElement] {
    guard let raw = copyAttribute(element, kAXChildrenAttribute as CFString),
          CFGetTypeID(raw) == CFArrayGetTypeID() else {
        return []
    }
    return Array((raw as! [AXUIElement]).prefix(limit))
}

private func collectText(_ element: AXUIElement, depth: Int = 0, output: inout [String]) {
    if depth > 4 || output.count > 160 {
        return
    }

    for attr in [kAXTitleAttribute, kAXValueAttribute, kAXDescriptionAttribute, kAXHelpAttribute] {
        let text = stringValue(copyAttribute(element, attr as CFString)).trimmingCharacters(in: .whitespacesAndNewlines)
        if !text.isEmpty {
            output.append(text)
        }
    }

    for child in children(element) {
        collectText(child, depth: depth + 1, output: &output)
    }
}

private func currentTypelessState() -> VoiceState {
    guard AXIsProcessTrusted() else {
        return VoiceState(active: false, phase: "unknown", message: "Typeless AX 未授权")
    }

    let apps = NSWorkspace.shared.runningApplications.filter {
        $0.bundleIdentifier == "now.typeless.desktop" || $0.localizedName == "Typeless"
    }
    guard let app = apps.first else {
        return VoiceState(active: false, phase: "idle", message: "Typeless 未运行")
    }

    let axApp = AXUIElementCreateApplication(app.processIdentifier)
    var texts: [String] = []
    collectText(axApp, output: &texts)

    if let rawWindows = copyAttribute(axApp, kAXWindowsAttribute as CFString),
       CFGetTypeID(rawWindows) == CFArrayGetTypeID() {
        for window in rawWindows as! [AXUIElement] {
            collectText(window, output: &texts)
        }
    }

    let joined = texts.joined(separator: " ").lowercased()
    let activeNeedles = ["正在录制", "录制中", "正在听", "正在听写", "recording", "listening", "dictating"]
    let processingNeedles = ["正在处理", "处理中", "正在转写", "转写中", "正在发送", "processing", "transcribing", "sending"]
    let sentNeedles = ["已发送", "发送完成", "已插入", "sent", "inserted", "completed", "done"]
    let idleNeedles = ["已停止", "停止", "未录制", "ready", "idle", "start recording", "press"]

    if activeNeedles.contains(where: { joined.contains($0) }) {
        return VoiceState(active: true, phase: "recording", message: "正在录制中")
    }
    if processingNeedles.contains(where: { joined.contains($0) }) {
        return VoiceState(active: true, phase: "processing", message: "")
    }
    if sentNeedles.contains(where: { joined.contains($0) }) {
        return VoiceState(active: false, phase: "idle", message: "Typeless sent")
    }
    if idleNeedles.contains(where: { joined.contains($0) }) {
        return VoiceState(active: false, phase: "idle", message: "Typeless 已停止")
    }

    return VoiceState(active: false, phase: "idle", message: "Typeless 待机")
}

private func frontmostFocusSnapshot() -> FocusSnapshot? {
    guard AXIsProcessTrusted(),
          let app = NSWorkspace.shared.frontmostApplication else {
        return nil
    }

    if app.bundleIdentifier == "now.typeless.desktop" || app.localizedName == "Typeless" {
        return nil
    }

    let axApp = AXUIElementCreateApplication(app.processIdentifier)
    var focusedWindow: AXUIElement?
    var windowTitle = ""

    if let rawWindow = copyAttribute(axApp, kAXFocusedWindowAttribute as CFString),
       CFGetTypeID(rawWindow) == AXUIElementGetTypeID() {
        focusedWindow = (rawWindow as! AXUIElement)
        windowTitle = stringValue(copyAttribute(focusedWindow!, kAXTitleAttribute as CFString))
    }

    return FocusSnapshot(
        processIdentifier: app.processIdentifier,
        bundleIdentifier: app.bundleIdentifier,
        appName: app.localizedName ?? "Unknown",
        window: focusedWindow,
        windowTitle: windowTitle
    )
}

private func focusedElement(for app: NSRunningApplication, axApp: AXUIElement) -> AXUIElement? {
    if let raw = copyAttribute(axApp, kAXFocusedUIElementAttribute as CFString),
       CFGetTypeID(raw) == AXUIElementGetTypeID() {
        return (raw as! AXUIElement)
    }

    let systemWide = AXUIElementCreateSystemWide()
    if let raw = copyAttribute(systemWide, kAXFocusedUIElementAttribute as CFString),
       CFGetTypeID(raw) == AXUIElementGetTypeID() {
        return (raw as! AXUIElement)
    }

    return nil
}

private func isEditableInputElement(_ element: AXUIElement) -> Bool {
    if let enabled = boolValue(copyAttribute(element, kAXEnabledAttribute as CFString)), !enabled {
        return false
    }

    let role = stringValue(copyAttribute(element, kAXRoleAttribute as CFString))
    let textRoles = [
        kAXTextFieldRole as String,
        kAXTextAreaRole as String,
        kAXComboBoxRole as String
    ]
    if textRoles.contains(role) {
        return true
    }

    if boolValue(copyAttribute(element, "AXEditable" as CFString)) == true {
        return true
    }

    var settable: DarwinBoolean = false
    if AXUIElementIsAttributeSettable(element, kAXValueAttribute as CFString, &settable) == .success,
       settable.boolValue {
        return copyAttribute(element, kAXSelectedTextRangeAttribute as CFString) != nil
    }

    return false
}

private func editableInputElement(from element: AXUIElement) -> AXUIElement? {
    var current: AXUIElement? = element
    for _ in 0..<5 {
        guard let candidate = current else {
            return nil
        }
        if isEditableInputElement(candidate) {
            return candidate
        }
        guard let rawParent = copyAttribute(candidate, kAXParentAttribute as CFString),
              CFGetTypeID(rawParent) == AXUIElementGetTypeID() else {
            return nil
        }
        current = (rawParent as! AXUIElement)
    }
    return nil
}

private func currentInputFocusTarget() -> InputFocusTarget? {
    guard AXIsProcessTrusted(),
          let app = NSWorkspace.shared.frontmostApplication else {
        return nil
    }

    if app.bundleIdentifier == "now.typeless.desktop" || app.localizedName == "Typeless" {
        return nil
    }

    let axApp = AXUIElementCreateApplication(app.processIdentifier)
    guard let focused = focusedElement(for: app, axApp: axApp),
          let input = editableInputElement(from: focused) else {
        return nil
    }

    var focusedWindow: AXUIElement?
    var windowTitle = ""
    if let rawWindow = copyAttribute(axApp, kAXFocusedWindowAttribute as CFString),
       CFGetTypeID(rawWindow) == AXUIElementGetTypeID() {
        focusedWindow = (rawWindow as! AXUIElement)
        windowTitle = stringValue(copyAttribute(focusedWindow!, kAXTitleAttribute as CFString))
    }

    let focus = FocusSnapshot(
        processIdentifier: app.processIdentifier,
        bundleIdentifier: app.bundleIdentifier,
        appName: app.localizedName ?? "Unknown",
        window: focusedWindow,
        windowTitle: windowTitle
    )
    let role = stringValue(copyAttribute(input, kAXRoleAttribute as CFString))
    let title = stringValue(copyAttribute(input, kAXTitleAttribute as CFString))
    return InputFocusTarget(focus: focus, element: input, role: role, title: title)
}

private func compactDuration(_ seconds: Int) -> String {
    let minutes = max(0, seconds / 60)
    if minutes >= 1440 {
        return "\(minutes / 1440)d \((minutes % 1440) / 60)h"
    }
    if minutes >= 60 {
        return "\(minutes / 60)h \(minutes % 60)m"
    }
    return "\(minutes)m"
}

private func percent(_ value: Any?) -> Int? {
    if let value = value as? Int {
        return max(0, min(100, value))
    }
    if let value = value as? Double {
        return max(0, min(100, Int(value.rounded())))
    }
    if let value = value as? String, let doubleValue = Double(value) {
        return max(0, min(100, Int(doubleValue.rounded())))
    }
    return nil
}

private func flattenDictionaries(_ value: Any) -> [[String: Any]] {
    var result: [[String: Any]] = []
    if let dict = value as? [String: Any] {
        result.append(dict)
        for child in dict.values {
            result.append(contentsOf: flattenDictionaries(child))
        }
    } else if let array = value as? [Any] {
        for child in array {
            result.append(contentsOf: flattenDictionaries(child))
        }
    }
    return result
}

private func leftPercent(from window: [String: Any]) -> Int? {
    for key in ["percent_left", "left_percent", "remaining_percent", "remaining_pct"] {
        if let value = percent(window[key]) {
            return value
        }
    }
    for key in ["used_percent", "usage_percent", "used_pct"] {
        if let used = percent(window[key]) {
            return max(0, min(100, 100 - used))
        }
    }
    return nil
}

private func resetText(from window: [String: Any]) -> String {
    // Prefer the server's relative value. It avoids local clock skew and is the
    // field the official usage response updates for the selected quota window.
    if let resetAfter = window["reset_after_seconds"] {
        if let value = resetAfter as? Int {
            return compactDuration(value)
        }
        if let value = resetAfter as? Double {
            return compactDuration(Int(value))
        }
        if let value = resetAfter as? String, let seconds = Double(value) {
            return compactDuration(Int(seconds))
        }
    }

    if let resetAt = window["reset_at"] {
        let timestamp: Double?
        if let value = resetAt as? Double {
            timestamp = value
        } else if let value = resetAt as? Int {
            timestamp = Double(value)
        } else if let value = resetAt as? String {
            timestamp = Double(value)
        } else {
            timestamp = nil
        }
        if let timestamp {
            return compactDuration(Int(max(0, timestamp - Date().timeIntervalSince1970)))
        }
    }
    return "--"
}

private func jsonObject(from data: Data) throws -> [String: Any] {
    let object = try JSONSerialization.jsonObject(with: data)
    guard let dict = object as? [String: Any] else {
        throw NSError(domain: "StopWatchBleBridge", code: 1, userInfo: [NSLocalizedDescriptionKey: "JSON root is not an object"])
    }
    return dict
}

private func codexAuth() throws -> (accessToken: String, accountId: String) {
    guard SettingsStore.shared.settings.enableCodexQuota else {
        throw NSError(domain: "StopWatchBleBridge", code: 4, userInfo: [NSLocalizedDescriptionKey: "Codex quota disabled"])
    }
    let authURL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex/auth.json")
    let payload = try jsonObject(from: Data(contentsOf: authURL))
    let tokens = payload["tokens"] as? [String: Any] ?? [:]
    guard let accessToken = tokens["access_token"] as? String, !accessToken.isEmpty else {
        throw NSError(domain: "StopWatchBleBridge", code: 2, userInfo: [NSLocalizedDescriptionKey: "missing Codex access token"])
    }
    return (accessToken, tokens["account_id"] as? String ?? "")
}

private func fetchCodexUsage(auth suppliedAuth: (accessToken: String, accountId: String)? = nil) throws -> [String: Any] {
    let auth = try suppliedAuth ?? codexAuth()
    var request = URLRequest(url: URL(string: "https://chatgpt.com/backend-api/wham/usage")!)
    request.httpMethod = "GET"
    request.setValue("application/json", forHTTPHeaderField: "Accept")
    request.setValue("Bearer \(auth.accessToken)", forHTTPHeaderField: "Authorization")
    request.setValue("stopwatch-ble-bridge/1.0", forHTTPHeaderField: "User-Agent")
    if !auth.accountId.isEmpty {
        request.setValue(auth.accountId, forHTTPHeaderField: "ChatGPT-Account-Id")
    }

    var result: Result<[String: Any], Error>?
    let semaphore = DispatchSemaphore(value: 0)
    URLSession.shared.dataTask(with: request) { data, response, error in
        defer { semaphore.signal() }
        if let error {
            result = .failure(error)
            return
        }
        if let http = response as? HTTPURLResponse, !(200..<300).contains(http.statusCode) {
            result = .failure(NSError(domain: "StopWatchBleBridge", code: http.statusCode, userInfo: [NSLocalizedDescriptionKey: "Codex usage HTTP \(http.statusCode)"]))
            return
        }
        do {
            result = .success(try jsonObject(from: data ?? Data()))
        } catch {
            result = .failure(error)
        }
    }.resume()

    if semaphore.wait(timeout: .now() + 20) == .timedOut {
        throw NSError(domain: "StopWatchBleBridge", code: 3, userInfo: [NSLocalizedDescriptionKey: "Codex usage request timed out"])
    }
    return try result!.get()
}

private struct DailyWeeklyQuotaState: Codable {
    var dayKey: String
    var periodStart: String
    var dayStartLeftPct: Int
    var segmentStartLeftPct: Int
    var previousLeftPct: Int
    var usedSinceStartPctPoints: Int
    var resetCount: Int
    var updatedAt: String

    var payload: [String: Any] {
        [
            "boundary_hour_local": 8,
            "day_key": dayKey,
            "period_start": periodStart,
            "day_start_left_pct": dayStartLeftPct,
            "segment_start_left_pct": segmentStartLeftPct,
            "current_left_pct": previousLeftPct,
            "used_since_start_pct_points": usedSinceStartPctPoints,
            "reset_count": resetCount,
            "updated_at": updatedAt
        ]
    }
}

private final class DailyWeeklyQuotaTracker {
    static let shared = DailyWeeklyQuotaTracker()

    private let lock = NSLock()
    private var state: DailyWeeklyQuotaState?

    private init() {
        state = load()
    }

    func update(weeklyLeftPct: Int, now: Date = Date()) -> DailyWeeklyQuotaState {
        lock.lock()
        defer { lock.unlock() }

        let left = max(0, min(100, weeklyLeftPct))
        let periodStart = trackingPeriodStart(for: now)
        let dayKey = trackingDayKey(for: periodStart)
        let nowText = ISO8601DateFormatter().string(from: now)

        if state?.dayKey != dayKey {
            state = DailyWeeklyQuotaState(
                dayKey: dayKey,
                periodStart: ISO8601DateFormatter().string(from: periodStart),
                dayStartLeftPct: left,
                segmentStartLeftPct: left,
                previousLeftPct: left,
                usedSinceStartPctPoints: 0,
                resetCount: 0,
                updatedAt: nowText
            )
        } else if var current = state {
            if left < current.previousLeftPct {
                current.usedSinceStartPctPoints += current.previousLeftPct - left
            } else if left > current.previousLeftPct {
                current.resetCount += 1
                current.segmentStartLeftPct = left
            }
            current.previousLeftPct = left
            current.updatedAt = nowText
            state = current
        }

        let result = state!
        save(result)
        return result
    }

    private func trackingPeriodStart(for now: Date) -> Date {
        var calendar = Calendar.current
        calendar.timeZone = TimeZone(identifier: "Asia/Shanghai")!
        let startOfDay = calendar.startOfDay(for: now)
        let todayBoundary = calendar.date(byAdding: .hour, value: 8, to: startOfDay) ?? startOfDay
        if now >= todayBoundary {
            return todayBoundary
        }
        return calendar.date(byAdding: .day, value: -1, to: todayBoundary) ?? todayBoundary
    }

    private func trackingDayKey(for periodStart: Date) -> String {
        let formatter = DateFormatter()
        formatter.calendar = Calendar.current
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = TimeZone(identifier: "Asia/Shanghai")!
        formatter.dateFormat = "yyyy-MM-dd"
        return formatter.string(from: periodStart)
    }

    private func load() -> DailyWeeklyQuotaState? {
        do {
            let data = try Data(contentsOf: quotaDailyStateFileURL)
            return try JSONDecoder().decode(DailyWeeklyQuotaState.self, from: data)
        } catch {
            return nil
        }
    }

    private func save(_ state: DailyWeeklyQuotaState) {
        do {
            try FileManager.default.createDirectory(at: supportDirectoryURL, withIntermediateDirectories: true)
            let data = try JSONEncoder().encode(state)
            try data.write(to: quotaDailyStateFileURL, options: .atomic)
        } catch {
            log("daily weekly quota state save failed: \(error.localizedDescription)")
        }
    }
}

private func weeklyWindow(_ raw: [String: Any]) throws -> [String: Any] {
    let rateLimit = raw["rate_limit"] as? [String: Any] ?? raw
    var windows: [Int: [String: Any]] = [:]
    for item in flattenDictionaries(rateLimit) {
        guard let seconds = (item["limit_window_seconds"] as? Int) ??
            ((item["limit_window_seconds"] as? String).flatMap { Int($0) })
        else {
            continue
        }
        if seconds == 604800 {
            windows[seconds] = item
        }
    }

    guard let weekly = windows[604800], leftPercent(from: weekly) != nil else {
        throw CloudSyncError.noSnapshot // Missing is not a 0% quota observation.
    }
    return weekly
}

private func buildDevicePanel() throws -> Data {
    let weeklyRemaining: Int
    let weeklyReset: String
    let weeklyResetAt: Any
    let dailyPayload: [String: Any]
    let activityBuckets: [Double]
    let stale: Bool
    let quotaUpdatedAt: String
    let source: String
    let message: String
    if CloudUsageSynchronizer.shared.configured {
        let auth = try codexAuth()
        let snapshot = try CloudUsageSynchronizer.shared.refresh(
            accountID: auth.accountId,
            activity: OpenWatcherHomeActivityTracker.shared.cloudEvents(),
            quotaInterval: TimeInterval(SettingsStore.shared.settings.quotaRefreshSeconds)
        ) {
            let weekly = try weeklyWindow(fetchCodexUsage(auth: auth))
            let reset = (weekly["reset_at"] as? NSNumber)?.doubleValue ??
                (weekly["reset_at"] as? String).flatMap(Double.init)
            return CloudQuotaObservation(at: Date().timeIntervalSince1970,
                                         left: Double(leftPercent(from: weekly)!), reset_at: reset)
        }
        guard snapshot.weekly.valid, let left = snapshot.weekly.left_pct else { throw CloudSyncError.noSnapshot }
        weeklyRemaining = Int(left.rounded())
        weeklyResetAt = snapshot.weekly.reset_at as Any? ?? NSNull()
        weeklyReset = snapshot.weekly.reset_at.map { compactDuration(Int(max(0, $0 - Date().timeIntervalSince1970))) } ?? "--"
        dailyPayload = snapshot.daily_tracking.payload
        activityBuckets = snapshot.activity_buckets
        stale = snapshot.stale
        quotaUpdatedAt = ISO8601DateFormatter().string(from: Date(timeIntervalSince1970: snapshot.weekly.observed_at ?? snapshot.as_of))
        source = "cloud_shared"
        message = stale ? "Quota cached" : (snapshot.daily_tracking.quality == "sampled" ? "Quota synced" :
            (snapshot.daily_tracking.quality == "imported" ? "Today estimate" : "Today incomplete"))
        if let error = CloudUsageSynchronizer.shared.lastError { log(error) }
    } else {
        let weekly = try weeklyWindow(fetchCodexUsage())
        weeklyRemaining = leftPercent(from: weekly)!
        weeklyReset = resetText(from: weekly)
        weeklyResetAt = weekly["reset_at"] ?? NSNull()
        dailyPayload = DailyWeeklyQuotaTracker.shared.update(weeklyLeftPct: weeklyRemaining).payload
        activityBuckets = OpenWatcherHomeActivityTracker.shared.buckets()
        stale = false
        quotaUpdatedAt = ISO8601DateFormatter().string(from: Date())
        source = "mac_bridge"
        message = "Quota synced"
    }

    let now = ISO8601DateFormatter().string(from: Date())
    let epoch = Int(Date().timeIntervalSince1970)
    let pressure = max(0, min(100, 100 - weeklyRemaining))
    let activeState = BridgeStatusCenter.shared.typelessStatus
    let sessionActive = activeState.lowercased().contains("record") ||
        activeState.contains("录制") ||
        activeState.lowercased().contains("processing") ||
        activeState.contains("处理")
    let openWatcherHome: [String: Any] = [
        "version": 1,
        "session_title": sessionActive ? "Typeless Voice" : "Codex Ready",
        "context_label": "\(100 - weeklyRemaining)% / WEEK",
        "context_pressure_pct": pressure,
        "compact_threshold_pct": 82,
        "compact_warning": pressure >= 82,
        "total_tokens_label": "--",
        "model_label": "Codex",
        "reasoning_label": currentCodexThinkingEffort(),
        "activity_label": sessionActive ? "live" : "4h",
        "activity_live": sessionActive,
        "activity_window": "4h",
        "activity_buckets": activityBuckets
    ]
    let codex: [String: Any] = [
        "valid": true,
        "status": stale ? "stale" : "ok",
        "source": source,
        "updated_at": quotaUpdatedAt,
        "stale": stale,
        "processing": false,
        "message": message,
        "host": [
            "name": Host.current().localizedName ?? "Mac",
            "app": "StopWatch BLE Bridge",
            "connected": true
        ],
        "session": [
            "state": sessionActive ? "active" : "idle",
            "active_title": sessionActive ? "Typeless Voice" : "",
            "active_for_sec": 0,
            "last_event": activeState.isEmpty ? "quota_sync" : activeState
        ],
        "openwatcher_home": openWatcherHome,
        "weekly": [
            "left_pct": weeklyRemaining,
            "used_pct": 100 - weeklyRemaining,
            "reset_in": weeklyReset,
            "reset_at": weeklyResetAt,
            "daily_tracking": dailyPayload
        ]
    ]
    log("quota weekly left=\(weeklyRemaining)% reset=\(weeklyReset) window=604800s")
    let panel: [String: Any] = [
        "version": 1,
        "device_id": codexDeviceId,
        "server_time": now,
        "server_epoch": epoch,
        "ttl_seconds": 900,
        "codex": codex,
        "pet": ["state": "idle", "message": "Ready", "processing": false],
        "features": ["wifi_enabled": true, "ble_enabled": true]
    ]
    return try JSONSerialization.data(withJSONObject: panel, options: [])
}

private func buildDeviceTimePanel() throws -> Data {
    let now = ISO8601DateFormatter().string(from: Date())
    let epoch = Int(Date().timeIntervalSince1970)
    let panel: [String: Any] = [
        "version": 1,
        "device_id": codexDeviceId,
        "server_time": now,
        "server_epoch": epoch,
        "ttl_seconds": 120,
        "codex": [
            "valid": false,
            "status": "time_only",
            "processing": false,
            "message": "Bridge time synced",
            "weekly": ["left_pct": -1, "reset_in": "--"]
        ],
        "pet": ["state": "idle", "message": "Bridge ready", "processing": false],
        "features": ["wifi_enabled": true, "ble_enabled": true]
    ]
    return try JSONSerialization.data(withJSONObject: panel, options: [])
}

private func syncTypelessDictationShortcutIfNeeded(_ settings: BridgeSettings) {
    guard settings.syncTypelessShortcut else { return }
    let url = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/Typeless/app-settings.json")
    do {
        let original = try Data(contentsOf: url)
        let backupURL = url.deletingLastPathComponent()
            .appendingPathComponent("app-settings.json.stopwatch-bridge.bak")
        if !FileManager.default.fileExists(atPath: backupURL.path) {
            try original.write(to: backupURL, options: .atomic)
        }
        var object = try jsonObject(from: original)
        var bindings = object["featureShortcutBindings"] as? [String: Any] ?? [:]
        let typelessLeftKey = keyBinding(named: settings.typelessLeftKeyName, fallback: "F19", custom: settings.customKeyBindings)
        var dictationMode = bindings["dictationMode"] as? [String] ?? []
        if !dictationMode.contains(typelessLeftKey.name) {
            dictationMode.append(typelessLeftKey.name)
        }
        bindings["dictationMode"] = dictationMode
        object["featureShortcutBindings"] = bindings
        let data = try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys])
        try data.write(to: url, options: .atomic)
        log("Typeless dictation shortcut appended: \(typelessLeftKey.name)")
    } catch {
        BridgeStatusCenter.shared.lastError = "Typeless shortcut sync failed: \(error.localizedDescription)"
        log("Typeless shortcut sync failed: \(error.localizedDescription)")
    }
}

private func hexString(_ data: Data) -> String {
    data.map { String(format: "%02x", $0) }.joined()
}

private final class SettingsWindowController: NSWindowController {
    private let inputModePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let leftPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let rightPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let confirmLongPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let shakePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let captureLeftButton = NSButton(title: "录入", target: nil, action: nil)
    private let captureRightButton = NSButton(title: "录入", target: nil, action: nil)
    private let captureConfirmLongButton = NSButton(title: "录入", target: nil, action: nil)
    private let captureShakeButton = NSButton(title: "录入", target: nil, action: nil)
    private let quotaField = NSTextField(frame: .zero)
    private let quotaCheckbox = NSButton(checkboxWithTitle: "同步 Codex 额度（使用本机登录状态）", target: nil, action: nil)
    private let syncTypelessCheckbox = NSButton(checkboxWithTitle: "同步 Typeless 快捷键到 A 键绑定", target: nil, action: nil)
    private let modeDescriptionLabel = NSTextField(labelWithString: "")
    private let leftHelpLabel = NSTextField(labelWithString: "")
    private let rightHelpLabel = NSTextField(labelWithString: "")
    private var draftSettings = BridgeSettings()
    private var editingMode: InputMode = .typeless

    init() {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 530),
                              styleMask: [.titled, .closable],
                              backing: .buffered,
                              defer: false)
        window.title = "StopWatch 桥接设置"
        window.center()
        super.init(window: window)
        buildContent()
        loadSettings()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func buildContent() {
        guard let content = window?.contentView else { return }
        content.wantsLayer = true
        content.layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor

        let title = NSTextField(labelWithString: "M5 StopWatch 桥接")
        title.font = .boldSystemFont(ofSize: 20)
        title.frame = NSRect(x: 28, y: 480, width: 360, height: 26)
        content.addSubview(title)

        let subtitle = NSTextField(labelWithString: "切换输入法模式时，会自动恢复该模式保存过的按键绑定。")
        subtitle.textColor = .secondaryLabelColor
        subtitle.frame = NSRect(x: 28, y: 454, width: 455, height: 20)
        content.addSubview(subtitle)

        let modeBox = makeCard(frame: NSRect(x: 20, y: 354, width: 480, height: 86))
        content.addSubview(modeBox)
        addSectionTitle("输入模式", x: 20, y: 52, to: modeBox)
        populateModePopup()
        inputModePopup.target = self
        inputModePopup.action = #selector(inputModeChanged)
        inputModePopup.frame = NSRect(x: 154, y: 47, width: 286, height: 30)
        modeBox.addSubview(inputModePopup)
        modeDescriptionLabel.textColor = .secondaryLabelColor
        modeDescriptionLabel.font = .systemFont(ofSize: 12)
        modeDescriptionLabel.frame = NSRect(x: 20, y: 18, width: 430, height: 18)
        modeBox.addSubview(modeDescriptionLabel)

        let keyBox = makeCard(frame: NSRect(x: 20, y: 186, width: 480, height: 152))
        content.addSubview(keyBox)
        addSectionTitle("按键绑定", x: 20, y: 102, to: keyBox)
        addLabel("A 键", x: 20, y: 66, to: keyBox)
        populateKeyPopup(leftPopup)
        leftPopup.frame = NSRect(x: 154, y: 61, width: 214, height: 30)
        keyBox.addSubview(leftPopup)
        captureLeftButton.target = self
        captureLeftButton.action = #selector(captureLeftKey)
        captureLeftButton.bezelStyle = .rounded
        captureLeftButton.frame = NSRect(x: 378, y: 61, width: 62, height: 30)
        keyBox.addSubview(captureLeftButton)
        addLabel("B 键 / 确认", x: 20, y: 26, to: keyBox)
        populateKeyPopup(rightPopup)
        rightPopup.frame = NSRect(x: 154, y: 21, width: 214, height: 30)
        keyBox.addSubview(rightPopup)
        captureRightButton.target = self
        captureRightButton.action = #selector(captureRightKey)
        captureRightButton.bezelStyle = .rounded
        captureRightButton.frame = NSRect(x: 378, y: 21, width: 62, height: 30)
        keyBox.addSubview(captureRightButton)
        leftHelpLabel.textColor = .secondaryLabelColor
        leftHelpLabel.font = .systemFont(ofSize: 11)
        leftHelpLabel.frame = NSRect(x: 304, y: 102, width: 145, height: 18)
        keyBox.addSubview(leftHelpLabel)

        let behaviorBox = makeCard(frame: NSRect(x: 20, y: 46, width: 480, height: 124))
        content.addSubview(behaviorBox)
        addSectionTitle("同步与动作", x: 20, y: 90, to: behaviorBox)
        addLabel("B 键长按", x: 20, y: 56, to: behaviorBox)
        populateActionPopup(confirmLongPopup)
        confirmLongPopup.frame = NSRect(x: 154, y: 51, width: 154, height: 30)
        behaviorBox.addSubview(confirmLongPopup)
        captureConfirmLongButton.target = self
        captureConfirmLongButton.action = #selector(captureConfirmLongKey)
        captureConfirmLongButton.bezelStyle = .rounded
        captureConfirmLongButton.frame = NSRect(x: 314, y: 51, width: 58, height: 30)
        behaviorBox.addSubview(captureConfirmLongButton)
        addLabel("摇晃动作", x: 20, y: 20, to: behaviorBox)
        populateShakePopup()
        shakePopup.frame = NSRect(x: 154, y: 15, width: 154, height: 30)
        behaviorBox.addSubview(shakePopup)
        captureShakeButton.target = self
        captureShakeButton.action = #selector(captureShakeKey)
        captureShakeButton.bezelStyle = .rounded
        captureShakeButton.frame = NSRect(x: 314, y: 15, width: 58, height: 30)
        behaviorBox.addSubview(captureShakeButton)
        addLabel("额度刷新", x: 384, y: 20, to: behaviorBox)
        quotaField.placeholderString = "300"
        quotaField.alignment = .right
        quotaField.frame = NSRect(x: 432, y: 18, width: 34, height: 24)
        behaviorBox.addSubview(quotaField)
        let seconds = NSTextField(labelWithString: "秒")
        seconds.textColor = .secondaryLabelColor
        seconds.frame = NSRect(x: 468, y: 20, width: 18, height: 20)
        behaviorBox.addSubview(seconds)

        quotaCheckbox.frame = NSRect(x: 28, y: 18, width: 250, height: 22)
        content.addSubview(quotaCheckbox)

        syncTypelessCheckbox.frame = NSRect(x: 278, y: 18, width: 214, height: 22)
        content.addSubview(syncTypelessCheckbox)

        let save = NSButton(title: "保存", target: self, action: #selector(saveSettings))
        save.bezelStyle = .rounded
        save.keyEquivalent = "\r"
        save.frame = NSRect(x: 402, y: 480, width: 86, height: 30)
        content.addSubview(save)
    }

    private func makeCard(frame: NSRect) -> NSView {
        let card = NSView(frame: frame)
        card.wantsLayer = true
        card.layer?.cornerRadius = 10
        card.layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
        card.layer?.borderColor = NSColor.separatorColor.withAlphaComponent(0.45).cgColor
        card.layer?.borderWidth = 1
        return card
    }

    private func addSectionTitle(_ text: String, x: CGFloat, y: CGFloat, to view: NSView) {
        let label = NSTextField(labelWithString: text)
        label.font = .boldSystemFont(ofSize: 13)
        label.frame = NSRect(x: x, y: y, width: 120, height: 20)
        view.addSubview(label)
    }

    private func addLabel(_ text: String, x: CGFloat, y: CGFloat, to view: NSView) {
        let label = NSTextField(labelWithString: text)
        label.textColor = .secondaryLabelColor
        label.frame = NSRect(x: x, y: y, width: 110, height: 20)
        view.addSubview(label)
    }

    private func populateModePopup() {
        inputModePopup.removeAllItems()
        for mode in [InputMode.typeless, InputMode.wechatIME] {
            inputModePopup.addItem(withTitle: inputModeTitles[mode] ?? mode.rawValue)
            inputModePopup.lastItem?.representedObject = mode.rawValue
        }
    }

    private func populateKeyPopup(_ popup: NSPopUpButton) {
        popup.removeAllItems()
        for binding in availableKeyBindings {
            popup.addItem(withTitle: displayKeyName(binding.name))
            popup.lastItem?.representedObject = binding.name
        }
        for binding in draftSettings.customKeyBindings where !availableKeyBindings.contains(where: { $0.name == binding.name }) {
            popup.addItem(withTitle: displayKeyName(binding.name))
            popup.lastItem?.representedObject = binding.name
        }
    }

    private func populateShakePopup() {
        populateActionPopup(shakePopup)
    }

    private func populateActionPopup(_ popup: NSPopUpButton) {
        popup.removeAllItems()
        for action in availableShakeActions {
            popup.addItem(withTitle: shakeActionTitles[action] ?? action)
            popup.lastItem?.representedObject = action
        }
        popup.menu?.addItem(.separator())
        for binding in availableKeyBindings + draftSettings.customKeyBindings {
            popup.addItem(withTitle: "按键：\(displayKeyName(binding.name))")
            popup.lastItem?.representedObject = "Key:\(binding.name)"
        }
    }

    private func displayKeyName(_ name: String) -> String {
        name == "Right Option" ? "右 Option" : name
    }

    private func selectRaw(_ raw: String, in popup: NSPopUpButton) {
        for item in popup.itemArray where item.representedObject as? String == raw {
            popup.select(item)
            return
        }
    }

    private func selectedRaw(in popup: NSPopUpButton, fallback: String) -> String {
        popup.selectedItem?.representedObject as? String ?? fallback
    }

    private func refreshBindingPopups(keepingLeft left: String?, right: String?, confirmLong: String?, shake: String?) {
        populateKeyPopup(leftPopup)
        populateKeyPopup(rightPopup)
        populateActionPopup(confirmLongPopup)
        populateShakePopup()
        if let left {
            selectRaw(left, in: leftPopup)
        }
        if let right {
            selectRaw(right, in: rightPopup)
        }
        if let confirmLong {
            selectRaw(confirmLong, in: confirmLongPopup)
        }
        if let shake {
            selectRaw(shake, in: shakePopup)
        }
    }

    private func addCustomBindingIfNeeded(_ binding: KeyBinding) {
        guard !availableKeyBindings.contains(where: { $0.name == binding.name }),
              !draftSettings.customKeyBindings.contains(where: { $0.name == binding.name }) else {
            return
        }
        draftSettings.customKeyBindings.append(binding)
    }

    private func captureKey(title: String, apply: @escaping (KeyBinding) -> Void) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = "请按下要绑定的键。F13-F20、右 Option 等特殊键可继续在下拉列表中选择。"
        alert.addButton(withTitle: "取消")
        var token: Any?
        token = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak alert, weak self] event in
            guard let self else { return event }
            guard let binding = capturedKeyBinding(from: event) else {
                NSSound.beep()
                return nil
            }
            self.addCustomBindingIfNeeded(binding)
            apply(binding)
            if let token {
                NSEvent.removeMonitor(token)
            }
            token = nil
            alert?.window.close()
            return nil
        }
        alert.runModal()
        if let token {
            NSEvent.removeMonitor(token)
        }
    }

    @objc private func captureLeftKey() {
        captureKey(title: "录入 A 键绑定") { [weak self] binding in
            guard let self else { return }
            let right = self.selectedRaw(in: self.rightPopup, fallback: "Return")
            let confirmLong = self.selectedRaw(in: self.confirmLongPopup, fallback: "Command+Return")
            let shake = self.selectedRaw(in: self.shakePopup, fallback: "Clear Input")
            self.refreshBindingPopups(keepingLeft: binding.name, right: right, confirmLong: confirmLong, shake: shake)
        }
    }

    @objc private func captureRightKey() {
        captureKey(title: "录入 B 键绑定") { [weak self] binding in
            guard let self else { return }
            let left = self.selectedRaw(in: self.leftPopup, fallback: "F19")
            let confirmLong = self.selectedRaw(in: self.confirmLongPopup, fallback: "Command+Return")
            let shake = self.selectedRaw(in: self.shakePopup, fallback: "Clear Input")
            self.refreshBindingPopups(keepingLeft: left, right: binding.name, confirmLong: confirmLong, shake: shake)
        }
    }

    @objc private func captureConfirmLongKey() {
        captureKey(title: "录入 B 键长按绑定") { [weak self] binding in
            guard let self else { return }
            let left = self.selectedRaw(in: self.leftPopup, fallback: "F19")
            let right = self.selectedRaw(in: self.rightPopup, fallback: "Return")
            let shake = self.selectedRaw(in: self.shakePopup, fallback: "Clear Input")
            self.refreshBindingPopups(keepingLeft: left, right: right, confirmLong: "Key:\(binding.name)", shake: shake)
        }
    }

    @objc private func captureShakeKey() {
        captureKey(title: "录入摇晃绑定") { [weak self] binding in
            guard let self else { return }
            let left = self.selectedRaw(in: self.leftPopup, fallback: "F19")
            let right = self.selectedRaw(in: self.rightPopup, fallback: "Return")
            let confirmLong = self.selectedRaw(in: self.confirmLongPopup, fallback: "Command+Return")
            self.refreshBindingPopups(keepingLeft: left, right: right, confirmLong: confirmLong, shake: "Key:\(binding.name)")
        }
    }

    private func saveBindingDraft(for mode: InputMode) {
        switch mode {
        case .typeless:
            draftSettings.typelessLeftKeyName = selectedRaw(in: leftPopup, fallback: "F19")
            draftSettings.typelessRightKeyName = selectedRaw(in: rightPopup, fallback: "Return")
        case .wechatIME:
            draftSettings.wechatLeftKeyName = selectedRaw(in: leftPopup, fallback: "Right Option")
            draftSettings.wechatRightKeyName = selectedRaw(in: rightPopup, fallback: "Return")
        }
    }

    private func loadBindingDraft(for mode: InputMode) {
        switch mode {
        case .typeless:
            selectRaw(draftSettings.typelessLeftKeyName, in: leftPopup)
            selectRaw(draftSettings.typelessRightKeyName, in: rightPopup)
            leftHelpLabel.stringValue = "默认 F19"
            rightHelpLabel.stringValue = ""
        case .wechatIME:
            selectRaw(draftSettings.wechatLeftKeyName, in: leftPopup)
            selectRaw(draftSettings.wechatRightKeyName, in: rightPopup)
            leftHelpLabel.stringValue = "默认右 Option"
            rightHelpLabel.stringValue = ""
        }
        modeDescriptionLabel.stringValue = inputModeDescriptions[mode] ?? ""
    }

    private func loadSettings() {
        draftSettings = SettingsStore.shared.settings
        editingMode = draftSettings.inputMode
        refreshBindingPopups(keepingLeft: nil, right: nil, confirmLong: nil, shake: nil)
        selectRaw(editingMode.rawValue, in: inputModePopup)
        loadBindingDraft(for: editingMode)
        selectRaw(draftSettings.confirmLongActionName, in: confirmLongPopup)
        selectRaw(draftSettings.shakeActionName, in: shakePopup)
        quotaField.stringValue = "\(draftSettings.quotaRefreshSeconds)"
        quotaCheckbox.state = draftSettings.enableCodexQuota ? .on : .off
        syncTypelessCheckbox.state = draftSettings.syncTypelessShortcut ? .on : .off
    }

    @objc private func inputModeChanged() {
        saveBindingDraft(for: editingMode)
        let raw = selectedRaw(in: inputModePopup, fallback: InputMode.typeless.rawValue)
        editingMode = InputMode(rawValue: raw) ?? .typeless
        draftSettings.inputModeName = editingMode.rawValue
        loadBindingDraft(for: editingMode)
    }

    @objc private func saveSettings() {
        saveBindingDraft(for: editingMode)
        var settings = draftSettings
        settings.inputModeName = editingMode.rawValue
        settings.confirmLongActionName = selectedRaw(in: confirmLongPopup, fallback: "Command+Return")
        settings.shakeActionName = selectedRaw(in: shakePopup, fallback: "Clear Input")
        settings.quotaRefreshSeconds = Int(quotaField.stringValue) ?? 300
        settings.enableCodexQuota = quotaCheckbox.state == .on
        settings.syncTypelessShortcut = syncTypelessCheckbox.state == .on
        SettingsStore.shared.save(settings)
        syncTypelessDictationShortcutIfNeeded(SettingsStore.shared.settings)
        BridgeStatusCenter.shared.selfCheckStatus = runBridgeSelfCheck().summary
        BridgeStatusCenter.shared.inputMode = SettingsStore.shared.settings.inputMode.rawValue
        BridgeStatusCenter.shared.quotaStatus = "Settings saved"
        NotificationCenter.default.post(name: bridgeSettingsChangedNotification, object: nil)
        window?.close()
    }
}

private final class BridgeAppDelegate: NSObject, NSApplicationDelegate {
    private var statusItem: NSStatusItem!
    private var settingsWindow: SettingsWindowController?
    private var bridge: StopWatchBleBridge?
    private let options: Options

    init(options: Options) {
        self.options = options
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        SettingsStore.shared.reload()
        if !FileManager.default.fileExists(atPath: configFileURL.path) {
            SettingsStore.shared.save(SettingsStore.shared.settings)
        }
        syncTypelessDictationShortcutIfNeeded(SettingsStore.shared.settings)
        updateSelfCheckStatus()
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem.button?.title = ""
        statusItem.button?.imagePosition = .imageOnly
        BridgeStatusCenter.shared.appDelegate = self
        refreshMenu()
        bridge = StopWatchBleBridge(options: options)
    }

    func refreshMenu() {
        guard let statusItem else { return }
        let status = BridgeStatusCenter.shared
        statusItem.button?.image = makeMenuBarDeviceIcon(active: status.bleStatus == "Bridge ready")
        statusItem.button?.toolTip = "StopWatch BLE Bridge: \(status.bleStatus)"

        let menu = NSMenu()
        let selfCheck = runBridgeSelfCheck()
        menu.addItem(NSMenuItem(title: "自检：\(selfCheck.summary)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "BLE: \(status.bleStatus)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "输入辅助：\(selfCheck.accessibility)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "Typeless: \(selfCheck.typeless)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "额度授权：\(selfCheck.quota)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "语音状态：\(status.typelessStatus)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "Codex: \(status.quotaStatus)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "M5 StopWatch Mic: \(status.microphoneStatus)", action: nil, keyEquivalent: ""))
        if !status.lastError.isEmpty {
            menu.addItem(NSMenuItem(title: "最近错误：\(status.lastError)", action: nil, keyEquivalent: ""))
        }
        for issue in selfCheck.issues.prefix(3) {
            menu.addItem(NSMenuItem(title: "问题：\(issue)", action: nil, keyEquivalent: ""))
        }
        menu.addItem(.separator())
        let settings = SettingsStore.shared.settings
        menu.addItem(NSMenuItem(title: "输入模式：\(inputModeTitles[settings.inputMode] ?? settings.inputMode.rawValue)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "A 键：\(settings.leftKey.name) / B 键：\(settings.rightKey.name)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "摇晃动作：\(shakeActionTitles[settings.shakeActionName] ?? settings.shakeActionName)", action: nil, keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "额度刷新：每 \(settings.quotaRefreshSeconds) 秒", action: nil, keyEquivalent: ""))
        menu.addItem(.separator())
        let microphoneItem = NSMenuItem(title: "启用 M5 StopWatch Mic", action: #selector(toggleVirtualMicrophone), keyEquivalent: "m")
        microphoneItem.state = settings.virtualMicrophoneEnabled ? .on : .off
        menu.addItem(microphoneItem)
        menu.addItem(NSMenuItem(title: "设置...", action: #selector(openSettings), keyEquivalent: ","))
        menu.addItem(NSMenuItem(title: "运行诊断", action: #selector(runDiagnostics), keyEquivalent: "d"))
        menu.addItem(NSMenuItem(title: "请求辅助功能权限", action: #selector(requestAccessibility), keyEquivalent: "a"))
        menu.addItem(NSMenuItem(title: "打开辅助功能设置", action: #selector(openAccessibilitySettings), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "打开日志", action: #selector(openLog), keyEquivalent: "l"))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "退出", action: #selector(quit), keyEquivalent: "q"))
        menu.items.forEach { $0.target = self }
        statusItem.menu = menu
    }

    @objc private func openSettings() {
        if settingsWindow == nil {
            settingsWindow = SettingsWindowController()
        }
        settingsWindow?.showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc private func toggleVirtualMicrophone() {
        var settings = SettingsStore.shared.settings
        settings.virtualMicrophoneEnabled.toggle()
        SettingsStore.shared.save(settings)
        bridge?.setVirtualMicrophoneEnabled(settings.virtualMicrophoneEnabled)
        refreshMenu()
    }

    @objc private func requestAccessibility() {
        let trusted = requestAccessibilityAccess()
        BridgeStatusCenter.shared.lastError = trusted ? "" : "Accessibility permission requested"
        updateSelfCheckStatus()
    }

    @objc private func openAccessibilitySettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {
            NSWorkspace.shared.open(url)
        }
    }

    @objc private func openLog() {
        if !FileManager.default.fileExists(atPath: logFileURL.path) {
            try? FileManager.default.createDirectory(at: logFileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            try? "StopWatch BLE Bridge log\n".write(to: logFileURL, atomically: true, encoding: .utf8)
        }
        NSWorkspace.shared.open(logFileURL)
    }

    @objc private func runDiagnostics() {
        updateSelfCheckStatus()
        let check = runBridgeSelfCheck()
        BridgeStatusCenter.shared.lastError = check.issues.first ?? ""
        log("diagnostics summary=\(check.summary) accessibility=\(check.accessibility) typeless=\(check.typeless) quota=\(check.quota) issues=\(check.issues.joined(separator: "; "))")
    }

    private func updateSelfCheckStatus() {
        BridgeStatusCenter.shared.selfCheckStatus = runBridgeSelfCheck().summary
    }

    @objc private func quit() {
        NSApp.terminate(nil)
    }
}

private struct PendingPanelTransfer {
    let chunks: [Data]
    let seq: Int
    let bytes: Int
    let kind: String
    let finalStatus: String
    let reason: String
    let characteristic: CBCharacteristic
    var index: Int = 0
}

private final class StopWatchBleBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private let options: Options
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var eventCharacteristic: CBCharacteristic?
    private var statusCharacteristic: CBCharacteristic?
    private var panelCharacteristic: CBCharacteristic?
    private var microphoneControlCharacteristic: CBCharacteristic?
    private var microphoneAudioCharacteristic: CBCharacteristic?
    private var microphoneStatsCharacteristic: CBCharacteristic?
    private var hidNotifyCharacteristics: [CBCharacteristic] = []
    private let microphonePipeline = StopWatchMicrophonePipeline()
    private var eventNotifyEnabled = false
    private var lastState: VoiceState?
    private var pollTimer: Timer?
    private var quotaTimer: Timer?
    private var healthTimer: Timer?
    private var scanRecoveryActive = false
    private var nameScanFallbackWorkItem: DispatchWorkItem?
    private var nativeHIDConnectWorkItem: DispatchWorkItem?
    private var authenticationRecoveryWorkItem: DispatchWorkItem?
    private var authenticationRecoveryAttempts = 0
    private var lastNativeHIDWaitLogAt = Date.distantPast
    private var panelSequence = 1
    private var quotaFetchInFlight = false
    private var lastQuotaPanelSentAt = Date.distantPast
    private var bridgeDiscoveryInFlight = false
    private var lastTimePanelPushAt = Date.distantPast
    private var typelessSessionActive = false
    private var processingUntil: Date?
    private var processingStartedAt: Date?
    private var typelessPrimaryIsDown = false
    private var wechatOptionDown = false
    private var wechatHeldBinding: KeyBinding?
    private var typelessPrimaryDownAt: Date?
    private var lastRediscoverAt = Date.distantPast
    private var lastMicrophoneRecoveryAt = Date.distantPast
    private var lastMicrophoneHealthLogAt = Date.distantPast
    private var microphoneResubscribePending = false
    private var microphoneOnDemandSupported: Bool?
    private var microphoneControlQueue: [MicrophoneControlCommand] = []
    private var activeMicrophoneControlCommand: MicrophoneControlCommand?
    private var microphoneControlWriteInFlight = false
    private var microphoneShortcutMonitor: Any?
    private var suppressNextMicrophoneShortcut = false
    private var typelessLaunchInFlight = false
    private var microphoneSessionFaulted = false
    private var microphoneRecordingStartedAt: Date?
    private var microphoneDroppedBaseline: UInt32 = 0
    private var microphoneDroppedBaselineAt: Date?
    private var lastMicrophoneTransportFaultAt: Date?
    private var microphoneTransportRecoveryWorkItem: DispatchWorkItem?
    private var lastBridgeConfigPayload: String?
    private var lastSentCodexUnreadCount: Int?
    private var lastSentCodexTasks: [CodexUnreadTask]?
    private var codexTasksSequence = 1
    private var statusWriteQueue: [(payload: String, label: String)] = []
    private var statusWriteInFlight = false
    private var panelTransfers: [PendingPanelTransfer] = []
    private var panelWriteInFlight = false
    private var reasoningFallbackQueue: [Bool] = []
    private var reasoningFallbackInFlight = false
    private var didPromptAccessibility = false

    init(options: Options) {
        self.options = options
        super.init()
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(settingsChanged),
                                               name: bridgeSettingsChangedNotification,
                                               object: nil)
        ensureAccessibilityPrompt(reason: "startup")
        startMicrophoneShortcutMonitor()
        central = CBCentralManager(delegate: self, queue: .main)
        // Collect even when the watch moves to another Mac.
        startQuotaLoop()
    }

    deinit {
        if let microphoneShortcutMonitor {
            NSEvent.removeMonitor(microphoneShortcutMonitor)
        }
        NotificationCenter.default.removeObserver(self)
    }

    private func startMicrophoneShortcutMonitor() {
        microphoneShortcutMonitor = NSEvent.addGlobalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard !event.isARepeat else { return }
            let keyCode = UInt16(event.keyCode)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                if self.suppressNextMicrophoneShortcut {
                    self.suppressNextMicrophoneShortcut = false
                    log("synthetic Typeless stop shortcut ignored by microphone wake monitor")
                    return
                }
                guard
                      SettingsStore.shared.settings.virtualMicrophoneEnabled,
                      SettingsStore.shared.settings.inputMode == .typeless,
                      SettingsStore.shared.settings.leftKey.macKeyCode == keyCode,
                      self.lastState?.phase != "recording",
                      !self.typelessSessionActive else {
                    return
                }
                if self.microphoneSessionFaulted {
                    self.clearMicrophoneSessionFaultForRetry(source: "Mac shortcut")
                }
                guard self.prepareMicrophoneForTypelessRecording(source: "Mac shortcut") else { return }
                log("microphone wake requested by Typeless shortcut")
            }
        }
    }

    @objc private func settingsChanged() {
        SettingsStore.shared.reload()
        if SettingsStore.shared.settings.inputMode != .wechatIME && wechatOptionDown {
            wechatOptionDown = false
            wechatHeldBinding = nil
        }
        BridgeStatusCenter.shared.selfCheckStatus = runBridgeSelfCheck().summary
        BridgeStatusCenter.shared.inputMode = SettingsStore.shared.settings.inputMode.rawValue
        setVirtualMicrophoneEnabled(SettingsStore.shared.settings.virtualMicrophoneEnabled)
        quotaTimer?.invalidate()
        quotaTimer = nil
        if statusCharacteristic != nil {
            sendBridgeHeartbeat(force: true)
        }
        if panelCharacteristic != nil {
            pushTimePanel(reason: "settings")
        }
        startQuotaLoop()
        BridgeStatusCenter.shared.refreshMenu()
    }

    private func ensureAccessibilityPrompt(reason: String) {
        guard !AXIsProcessTrusted(), !didPromptAccessibility else {
            return
        }
        didPromptAccessibility = true
        let trusted = requestAccessibilityAccess()
        log(trusted ? "Accessibility already authorized at \(reason)." : "Accessibility authorization requested at \(reason).")
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            startHealthLoop()
            scanRecoveryActive = false
            recoverConnection(reason: "Bluetooth ready")
        default:
            nameScanFallbackWorkItem?.cancel()
            nameScanFallbackWorkItem = nil
            nativeHIDConnectWorkItem?.cancel()
            nativeHIDConnectWorkItem = nil
            authenticationRecoveryWorkItem?.cancel()
            authenticationRecoveryWorkItem = nil
            scanRecoveryActive = false
            BridgeStatusCenter.shared.bleStatus = "BLE unavailable"
            log("BLE unavailable: \(central.state.rawValue)")
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let advName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        guard isSupportedDeviceName(peripheral.name) || isSupportedDeviceName(advName) else { return }

        // Never race macOS' native HOGP pairing agent. Only attach the custom
        // bridge/audio services after IOHID confirms that the native keyboard
        // link is already paired and owned by the system. CoreBluetooth does
        // not return a system-owned HID link from retrieveConnectedPeripherals,
        // so this IORegistry gate is the reliable hand-off point.
        guard isNativeCodexHIDConnected() else {
            BridgeStatusCenter.shared.bleStatus = "Waiting for system connection"
            if Date().timeIntervalSince(lastNativeHIDWaitLogAt) >= 10 {
                lastNativeHIDWaitLogAt = Date()
                log("Found \(peripheral.name ?? advName ?? deviceNamePrefix), RSSI \(RSSI). Waiting for macOS HID pairing/connection...")
            }
            return
        }
        connectAfterNativeHIDSettles(peripheral,
                                     status: "Connecting",
                                     reason: "paired device discovered at RSSI \(RSSI)")
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        nativeHIDConnectWorkItem?.cancel()
        nativeHIDConnectWorkItem = nil
        nameScanFallbackWorkItem?.cancel()
        nameScanFallbackWorkItem = nil
        scanRecoveryActive = false
        resetBridgeCharacteristics()
        BridgeStatusCenter.shared.bleStatus = "Connected"
        log("Connected. Discovering bridge and microphone services...")
        discoverBridgeServices(peripheral, reason: "connect")
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        nativeHIDConnectWorkItem?.cancel()
        nativeHIDConnectWorkItem = nil
        BridgeStatusCenter.shared.bleStatus = "Connect failed"
        BridgeStatusCenter.shared.lastError = error?.localizedDescription ?? "unknown"
        log("Connect failed: \(error?.localizedDescription ?? "unknown")")
        resetBridgeCharacteristics()
        self.peripheral = nil
        if options.once {
            exit(1)
        }
        scanRecoveryActive = false
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            self?.recoverConnection(reason: "connect failed")
        }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        nativeHIDConnectWorkItem?.cancel()
        nativeHIDConnectWorkItem = nil
        authenticationRecoveryWorkItem?.cancel()
        authenticationRecoveryWorkItem = nil
        authenticationRecoveryAttempts = 0
        let interruptedRecording = SettingsStore.shared.settings.virtualMicrophoneEnabled &&
            SettingsStore.shared.settings.inputMode == .typeless &&
            (typelessSessionActive || lastState?.phase == "recording")
        BridgeStatusCenter.shared.bleStatus = "Disconnected"
        log("Disconnected: \(error?.localizedDescription ?? "normal")")
        pollTimer?.invalidate()
        pollTimer = nil
        // Keep account collection/cloud sync alive without a BLE connection.
        resetBridgeCharacteristics()
        if SettingsStore.shared.settings.virtualMicrophoneEnabled {
            // Tear down the Core Audio route as well as BLE state.  Keeping the
            // old AVAudioEngine alive across a radio reconnect can leave a
            // virtual input that renders silence while still reporting healthy.
            microphonePipeline.stop()
            BridgeStatusCenter.shared.microphoneStatus = interruptedRecording ? "Interrupted" : "Waiting for BLE"
        } else {
            microphonePipeline.stop()
            BridgeStatusCenter.shared.microphoneStatus = "Off"
        }
        self.peripheral = nil
        if interruptedRecording {
            triggerMicrophoneSessionFault(reason: "BLE disconnected during recording", stopTypeless: true)
        }
        if options.once {
            exit(0)
        }
        scanRecoveryActive = false
        recoverConnection(reason: "disconnect")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("Discover services failed: \(error.localizedDescription)")
            exit(1)
        }
        bridgeDiscoveryInFlight = false
        guard let services = peripheral.services,
              let service = services.first(where: { $0.uuid == serviceUUID }) else {
            BridgeStatusCenter.shared.bleStatus = "Bridge service missing"
            log("Bridge service not found. Check firmware UUID / Bluetooth setting.")
            exit(1)
        }
        peripheral.discoverCharacteristics([eventUUID, statusUUID, panelUUID], for: service)
        if let hidService = services.first(where: { $0.uuid == hidServiceUUID }) {
            // The firmware exposes several Report (2A4D) characteristics. Subscribe
            // to every notifying report so the vendor Codex Micro channel is
            // restored as well as the standard keyboard/consumer reports after a
            // BLE reconnect.
            peripheral.discoverCharacteristics([hidReportUUID], for: hidService)
        }
        if let microphoneService = services.first(where: { $0.uuid == stopWatchMicrophoneServiceUUID }) {
            peripheral.discoverCharacteristics(
                [stopWatchMicrophoneControlUUID, stopWatchMicrophoneAudioUUID, stopWatchMicrophoneStatsUUID],
                for: microphoneService
            )
        } else if SettingsStore.shared.settings.virtualMicrophoneEnabled {
            BridgeStatusCenter.shared.microphoneStatus = "Firmware service missing"
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let error {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("Discover characteristics failed: \(error.localizedDescription)")
            exit(1)
        }
        bridgeDiscoveryInFlight = false

        if service.uuid == hidServiceUUID {
            hidNotifyCharacteristics = (service.characteristics ?? []).filter {
                $0.uuid == hidReportUUID && $0.properties.contains(.notify)
            }
            for characteristic in hidNotifyCharacteristics where !characteristic.isNotifying {
                peripheral.setNotifyValue(true, for: characteristic)
            }
            log("HID report recovery: subscribing to \(hidNotifyCharacteristics.count) notify characteristic(s).")
            return
        }

        if service.uuid == stopWatchMicrophoneServiceUUID {
            for characteristic in service.characteristics ?? [] {
                switch characteristic.uuid {
                case stopWatchMicrophoneControlUUID: microphoneControlCharacteristic = characteristic
                case stopWatchMicrophoneAudioUUID: microphoneAudioCharacteristic = characteristic
                case stopWatchMicrophoneStatsUUID: microphoneStatsCharacteristic = characteristic
                default: break
                }
            }
            if SettingsStore.shared.settings.virtualMicrophoneEnabled {
                setVirtualMicrophoneEnabled(true)
            }
            return
        }

        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == eventUUID {
                eventCharacteristic = characteristic
                log("Event characteristic properties: \(characteristic.properties.rawValue)")
                peripheral.setNotifyValue(true, for: characteristic)
                log("Subscribing to device event stream.")
            } else if characteristic.uuid == statusUUID {
                statusCharacteristic = characteristic
                BridgeStatusCenter.shared.bleStatus = "Bridge ready"
                log("Status characteristic ready.")
                sendBridgeHeartbeat(force: true)
                refreshCodexUnreadStatus(force: true)
                if !AXIsProcessTrusted() {
                    sendBridgeLimited()
                }
                startStatusLoop()
            } else if characteristic.uuid == panelUUID {
                panelCharacteristic = characteristic
                log("Panel characteristic ready.")
                pushTimePanel(reason: "connect")
                startQuotaLoop()
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        if characteristic.uuid == hidReportUUID {
            if let error {
                BridgeStatusCenter.shared.lastError = error.localizedDescription
                log("HID report recovery subscription failed: \(error.localizedDescription)")
            } else {
                log(characteristic.isNotifying
                    ? "HID report recovery subscription ready."
                    : "HID report recovery subscription stopped.")
            }
            return
        }
        if characteristic.uuid == stopWatchMicrophoneAudioUUID || characteristic.uuid == stopWatchMicrophoneStatsUUID {
            if let error {
                microphoneResubscribePending = false
                BridgeStatusCenter.shared.microphoneStatus = "Subscribe failed"
                BridgeStatusCenter.shared.lastError = error.localizedDescription
                log("Microphone subscription failed: \(error.localizedDescription)")
                scheduleAuthenticationRecoveryIfNeeded(error, source: "microphone")
                sendBridgeHeartbeat()
                return
            }
            if characteristic.uuid == stopWatchMicrophoneAudioUUID {
                if !characteristic.isNotifying,
                   microphoneResubscribePending,
                   SettingsStore.shared.settings.virtualMicrophoneEnabled {
                    peripheral.setNotifyValue(true, for: characteristic)
                    return
                }
                if characteristic.isNotifying,
                   SettingsStore.shared.settings.virtualMicrophoneEnabled {
                    clearAuthenticationRecovery()
                    microphoneResubscribePending = false
                    queueMicrophoneControl(.armOnDemand)
                    BridgeStatusCenter.shared.microphoneStatus = "Ready"
                }
            }
            sendBridgeHeartbeat()
            return
        }
        guard characteristic.uuid == eventUUID else { return }
        if let error {
            eventNotifyEnabled = false
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("Event subscription failed: \(error.localizedDescription)")
            scheduleAuthenticationRecoveryIfNeeded(error, source: "event stream")
            return
        }
        eventNotifyEnabled = characteristic.isNotifying
        if eventNotifyEnabled {
            clearAuthenticationRecovery()
        }
        log(eventNotifyEnabled ? "Device event stream subscribed." : "Device event stream unsubscribed.")
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("Event read failed: \(error.localizedDescription)")
            return
        }
        if characteristic.uuid == stopWatchMicrophoneAudioUUID, let data = characteristic.value {
            if !microphonePipeline.processAudioPacket(data) {
                BridgeStatusCenter.shared.lastError = "Invalid microphone packet"
            }
            return
        }
        if characteristic.uuid == stopWatchMicrophoneStatsUUID,
           let data = characteristic.value,
           let stats = microphonePipeline.statsDescription(data) {
            log("microphone stats \(stats)")
            return
        }
        if characteristic.uuid == hidReportUUID {
            // macOS consumes HID reports through the system HID stack. This
            // CoreBluetooth subscription is only a reconnect recovery aid.
            return
        }
        guard characteristic.uuid == eventUUID,
              let data = characteristic.value,
              let text = String(data: data, encoding: .utf8) else {
            return
        }
        log("event \(text)")
        handleDeviceEvent(text)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if characteristic.uuid == stopWatchMicrophoneControlUUID {
            let command = activeMicrophoneControlCommand
            activeMicrophoneControlCommand = nil
            microphoneControlWriteInFlight = false
            handleMicrophoneControlResult(command, error: error)
            pumpGattWrites()
            return
        }
        if characteristic.uuid == statusUUID {
            statusWriteInFlight = false
            if let error {
                BridgeStatusCenter.shared.lastError = error.localizedDescription
                log("status write failed: \(error.localizedDescription)")
            }
            pumpGattWrites()
            return
        }
        if characteristic.uuid == panelUUID {
            panelWriteInFlight = false
            if let error {
                BridgeStatusCenter.shared.lastError = error.localizedDescription
                log("panel write failed: \(error.localizedDescription)")
                if !panelTransfers.isEmpty {
                    panelTransfers.removeFirst()
                }
            }
            pumpGattWrites()
        }
    }

    private func handleDeviceEvent(_ text: String) {
        if text.hasPrefix("codex_open:") {
            openCodexTask(String(text.dropFirst("codex_open:".count)))
            return
        }
        if text == "codex_reasoning:increase" || text == "codex_reasoning:decrease" {
            enqueueCodexReasoningDelta(text.hasSuffix("increase") ? 1 : -1)
            return
        }
        if text.hasPrefix("codex_reasoning:delta:"),
           let delta = Int(text.dropFirst("codex_reasoning:delta:".count)),
           delta != 0 {
            enqueueCodexReasoningDelta(delta)
            return
        }
        if text == "codex_reasoning:native_sync" {
            log("native Codex Micro reasoning event observed; scheduling confirmed-state refresh")
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { [weak self] in
                self?.pushQuotaPanel(reason: "reasoning-native")
            }
            return
        }
        switch text {
        case "input_primary_down":
            handlePrimaryInputDown()
        case "input_primary_up":
            handlePrimaryInputUp()
        case "input_primary_tap":
            log("legacy primary tap event ignored after hold/release protocol cleanup")
        case "input_confirm_tap":
            handleCodexEnterRequest()
        case "shake_action":
            handleShakeActionRequest()
        case "codex_new":
            openNewCodexTask()
        case "typeless_option_tap_host", "typeless_option_down", "typeless_option_up", "typeless_option_tap", "codex_enter":
            log("legacy bridge event ignored after input protocol cleanup: \(text)")
        default:
            return
        }
    }

    private func openCodexTask(_ id: String) {
        guard isSafeCodexThreadID(id),
              let url = URL(string: "codex://threads/\(id)") else {
            log("rejected invalid Codex task id")
            return
        }
        NSWorkspace.shared.open(url)
        log("opened Codex task \(id)")
    }

    private func openNewCodexTask() {
        let running = NSWorkspace.shared.runningApplications.first {
            $0.bundleIdentifier == "com.openai.codex"
        }
        if let running {
            running.activate(options: [.activateAllWindows])
        } else if let url = URL(string: "codex://") {
            NSWorkspace.shared.open(url)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
            guard let source = CGEventSource(stateID: .hidSystemState),
                  let down = CGEvent(keyboardEventSource: source,
                                     virtualKey: CGKeyCode(kVK_ANSI_N),
                                     keyDown: true),
                  let up = CGEvent(keyboardEventSource: source,
                                   virtualKey: CGKeyCode(kVK_ANSI_N),
                                   keyDown: false) else {
                log("could not synthesize Codex new task shortcut")
                return
            }
            down.flags = .maskCommand
            up.flags = .maskCommand
            down.post(tap: .cghidEventTap)
            up.post(tap: .cghidEventTap)
            log("requested new Codex task")
        }
    }

    private func enqueueCodexReasoningDelta(_ delta: Int) {
        let boundedDelta = max(-8, min(8, delta))
        guard boundedDelta != 0 else { return }
        reasoningFallbackQueue.append(contentsOf: repeatElement(boundedDelta > 0,
                                                                 count: abs(boundedDelta)))
        log("queued Codex reasoning fallback delta \(boundedDelta); pending=\(reasoningFallbackQueue.count)")
        pumpCodexReasoningFallback()
    }

    private func pumpCodexReasoningFallback() {
        guard !reasoningFallbackInFlight, !reasoningFallbackQueue.isEmpty else { return }
        reasoningFallbackInFlight = true
        let increase = reasoningFallbackQueue.removeFirst()
        changeCodexReasoning(increase: increase) { [weak self] in
            guard let self else { return }
            self.reasoningFallbackInFlight = false
            if self.reasoningFallbackQueue.isEmpty {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.75) { [weak self] in
                    self?.pushQuotaPanel(reason: "reasoning-fallback")
                }
            } else {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
                    self?.pumpCodexReasoningFallback()
                }
            }
        }
    }

    private func changeCodexReasoning(increase: Bool,
                                      completion: @escaping () -> Void) {
        let command = increase ? "Increase reasoning effort" : "Decrease reasoning effort"
        let running = NSWorkspace.shared.runningApplications.first {
            $0.bundleIdentifier == "com.openai.codex"
        }
        if let running {
            running.activate(options: [.activateAllWindows])
        } else if let url = URL(string: "codex://") {
            NSWorkspace.shared.open(url)
        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
            guard let source = CGEventSource(stateID: .hidSystemState) else {
                log("could not create Codex reasoning event source")
                completion()
                return
            }
            self?.postKey(CGKeyCode(kVK_ANSI_P), flags: [.maskCommand, .maskShift], source: source)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.18) { [weak self] in
                guard let textEvent = CGEvent(keyboardEventSource: source,
                                              virtualKey: 0,
                                              keyDown: true) else {
                    log("could not type Codex reasoning command")
                    completion()
                    return
                }
                textEvent.keyboardSetUnicodeString(stringLength: command.utf16.count,
                                                   unicodeString: Array(command.utf16))
                textEvent.post(tap: .cghidEventTap)
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.18) { [weak self] in
                    self?.postKey(CGKeyCode(kVK_Return), flags: [], source: source)
                    log("requested Codex command: \(command)")
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.35,
                                                   execute: completion)
                }
            }
        }
    }

    private func postKey(_ key: CGKeyCode,
                         flags: CGEventFlags,
                         source: CGEventSource) {
        guard let down = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: true),
              let up = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: false) else {
            return
        }
        down.flags = flags
        up.flags = flags
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
    }

    private func scanForDevice(hidServiceOnly: Bool) {
        guard central.state == .poweredOn else { return }
        let services = hidServiceOnly ? [hidServiceUUID] : nil
        central.scanForPeripherals(withServices: services,
                                   // The first advertisement often arrives before macOS has
                                   // finished exposing the native HID device. Keep duplicate
                                   // callbacks enabled only while scanning so the custom audio
                                   // service can attach immediately after the HID hand-off,
                                   // instead of waiting for the five-second health loop.
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
    }

    private func connectAfterNativeHIDSettles(_ candidate: CBPeripheral,
                                               status: String,
                                               reason: String) {
        guard peripheral == nil else { return }
        peripheral = candidate
        candidate.delegate = self
        nameScanFallbackWorkItem?.cancel()
        nameScanFallbackWorkItem = nil
        scanRecoveryActive = false
        central.stopScan()
        BridgeStatusCenter.shared.bleStatus = "Waiting for system connection"
        log("Native HID ready. Waiting \(nativeHIDBridgeSettleSeconds)s before bridge attach (\(reason))...")

        let work = DispatchWorkItem { [weak self, weak candidate] in
            guard let self, let candidate, self.peripheral === candidate else { return }
            self.nativeHIDConnectWorkItem = nil
            guard isNativeCodexHIDConnected() else {
                self.peripheral = nil
                self.scanRecoveryActive = false
                self.logNativeHIDNotReady(reason: reason)
                self.recoverConnection(reason: "native HID hand-off lost")
                return
            }
            BridgeStatusCenter.shared.bleStatus = status
            log("Connecting bridge after native HID hand-off (\(reason))...")
            self.central.connect(candidate)
        }
        nativeHIDConnectWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + nativeHIDBridgeSettleSeconds,
                                      execute: work)
    }

    private func logNativeHIDNotReady(reason: String) {
        if Date().timeIntervalSince(lastNativeHIDWaitLogAt) >= 10 {
            lastNativeHIDWaitLogAt = Date()
            log("Waiting for macOS native HID before bridge attach (\(reason))...")
        }
        BridgeStatusCenter.shared.bleStatus = "Waiting for system connection"
    }

    private func scheduleAuthenticationRecoveryIfNeeded(_ error: Error, source: String) {
        let nsError = error as NSError
        guard nsError.domain == CBATTErrorDomain, nsError.code == 0x05 else { return }
        guard authenticationRecoveryWorkItem == nil else { return }

        authenticationRecoveryAttempts += 1
        guard authenticationRecoveryAttempts <= authenticationRetryLimit else {
            BridgeStatusCenter.shared.bleStatus = "Pairing reset required"
            BridgeStatusCenter.shared.lastError = "Bluetooth authentication did not recover"
            log("Authentication retry limit reached after \(source); preserving the connection without another pairing prompt.")
            return
        }

        BridgeStatusCenter.shared.bleStatus = "Waiting for authentication"
        log("Authentication not ready for \(source). Retrying subscriptions in \(authenticationRetrySeconds)s (\(authenticationRecoveryAttempts)/\(authenticationRetryLimit))...")
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.authenticationRecoveryWorkItem = nil
            guard let peripheral = self.peripheral,
                  peripheral.state == .connected,
                  isNativeCodexHIDConnected() else {
                self.logNativeHIDNotReady(reason: "authentication retry")
                return
            }
            if let event = self.eventCharacteristic, !event.isNotifying {
                peripheral.setNotifyValue(true, for: event)
            }
            if SettingsStore.shared.settings.virtualMicrophoneEnabled {
                if let audio = self.microphoneAudioCharacteristic, !audio.isNotifying {
                    peripheral.setNotifyValue(true, for: audio)
                }
                if let stats = self.microphoneStatsCharacteristic, !stats.isNotifying {
                    peripheral.setNotifyValue(true, for: stats)
                }
            }
        }
        authenticationRecoveryWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + authenticationRetrySeconds,
                                      execute: work)
    }

    private func clearAuthenticationRecovery() {
        authenticationRecoveryWorkItem?.cancel()
        authenticationRecoveryWorkItem = nil
        authenticationRecoveryAttempts = 0
    }

    private func recoverConnection(reason: String) {
        guard central.state == .poweredOn, peripheral == nil else { return }

        let connected = central.retrieveConnectedPeripherals(withServices: [serviceUUID]) +
            central.retrieveConnectedPeripherals(withServices: [hidServiceUUID])
        if let candidate = connected.first(where: { isSupportedDeviceName($0.name) }) {
            if isNativeCodexHIDConnected() {
                connectAfterNativeHIDSettles(candidate,
                                             status: "Reconnecting",
                                             reason: reason)
                return
            }
            logNativeHIDNotReady(reason: reason)
        }

        guard !scanRecoveryActive else { return }
        scanRecoveryActive = true
        BridgeStatusCenter.shared.bleStatus = "Scanning"
        log("Scanning for \(deviceNamePrefix)* HID service (\(reason))...")
        scanForDevice(hidServiceOnly: true)

        let fallback = DispatchWorkItem { [weak self] in
            guard let self,
                  self.peripheral == nil,
                  self.central.state == .poweredOn else { return }
            log("HID scan did not find device. Falling back to name scan (\(reason))...")
            self.scanForDevice(hidServiceOnly: false)
        }
        nameScanFallbackWorkItem = fallback
        DispatchQueue.main.asyncAfter(deadline: .now() + 4.0, execute: fallback)
    }

    private func resetBridgeCharacteristics() {
        eventCharacteristic = nil
        statusCharacteristic = nil
        panelCharacteristic = nil
        microphoneControlCharacteristic = nil
        microphoneAudioCharacteristic = nil
        microphoneStatsCharacteristic = nil
        hidNotifyCharacteristics.removeAll()
        eventNotifyEnabled = false
        lastRediscoverAt = Date.distantPast
        lastBridgeConfigPayload = nil
        statusWriteQueue.removeAll()
        statusWriteInFlight = false
        panelTransfers.removeAll()
        panelWriteInFlight = false
        bridgeDiscoveryInFlight = false
        microphoneResubscribePending = false
        microphoneOnDemandSupported = nil
        microphoneControlQueue.removeAll()
        activeMicrophoneControlCommand = nil
        microphoneControlWriteInFlight = false
        microphoneTransportRecoveryWorkItem?.cancel()
        microphoneTransportRecoveryWorkItem = nil
        reasoningFallbackQueue.removeAll()
        reasoningFallbackInFlight = false
    }

    private func discoverBridgeServices(_ peripheral: CBPeripheral, reason: String) {
        guard !bridgeDiscoveryInFlight else {
            log("Bridge discovery already in flight; skip \(reason).")
            return
        }
        bridgeDiscoveryInFlight = true
        lastRediscoverAt = Date()
        peripheral.discoverServices([serviceUUID, stopWatchMicrophoneServiceUUID, hidServiceUUID])
    }

    func setVirtualMicrophoneEnabled(_ enabled: Bool) {
        guard enabled else {
            microphoneResubscribePending = false
            queueMicrophoneControl(.disable)
            if let peripheral, let audio = microphoneAudioCharacteristic, audio.isNotifying {
                peripheral.setNotifyValue(false, for: audio)
            }
            if let peripheral, let stats = microphoneStatsCharacteristic, stats.isNotifying {
                peripheral.setNotifyValue(false, for: stats)
            }
            microphonePipeline.stop()
            BridgeStatusCenter.shared.microphoneStatus = "Off"
            sendBridgeHeartbeat()
            return
        }

        do {
            let deviceName = try microphonePipeline.start()
            log("microphone output pinned: \(microphonePipeline.health().outputRouteDescription)")
            BridgeStatusCenter.shared.microphoneStatus = deviceName == stopWatchVirtualMicrophoneName
                ? "Ready" : "Ready (BlackHole fallback)"
        } catch {
            BridgeStatusCenter.shared.microphoneStatus = "Output unavailable"
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("microphone enable failed: \(error.localizedDescription)")
            return
        }
        guard let peripheral, let audio = microphoneAudioCharacteristic else {
            BridgeStatusCenter.shared.microphoneStatus = "Waiting for BLE"
            sendBridgeHeartbeat()
            return
        }
        if !audio.isNotifying {
            peripheral.setNotifyValue(true, for: audio)
        } else {
            queueMicrophoneControl(.armOnDemand)
        }
        if let stats = microphoneStatsCharacteristic, !stats.isNotifying {
            peripheral.setNotifyValue(true, for: stats)
        }
    }

    private func queueMicrophoneControl(_ command: MicrophoneControlCommand) {
        if command == .startVoice || command == .continuous {
            guard !microphoneSessionFaulted, microphonePipeline.isRunning else {
                log("microphone start blocked: virtual output is not verified or session is interrupted")
                return
            }
        }
        if (command == .startVoice || command == .stopVoice), microphoneOnDemandSupported != true {
            return
        }
        if activeMicrophoneControlCommand == command || microphoneControlQueue.last == command {
            return
        }
        microphoneControlQueue.append(command)
        pumpGattWrites()
    }

    private func sendNextMicrophoneControl() {
        guard !microphoneControlWriteInFlight,
              !statusWriteInFlight,
              !panelWriteInFlight,
              !microphoneControlQueue.isEmpty,
              let peripheral,
              let control = microphoneControlCharacteristic else {
            return
        }
        let command = microphoneControlQueue.removeFirst()
        if command == .startVoice || command == .continuous {
            guard !microphoneSessionFaulted, microphonePipeline.isRunning else {
                log("queued microphone start discarded: virtual output lost before BLE write")
                // Let the write pump continue without recursively growing the stack.
                DispatchQueue.main.async { [weak self] in self?.pumpGattWrites() }
                return
            }
            microphonePipeline.prepareForStreamRestart()
        }
        let type: CBCharacteristicWriteType = control.properties.contains(.write) ? .withResponse : .withoutResponse
        activeMicrophoneControlCommand = command
        microphoneControlWriteInFlight = type == .withResponse
        peripheral.writeValue(Data([command.rawValue]), for: control, type: type)
        log("microphone control: \(command)")
        if type == .withoutResponse {
            activeMicrophoneControlCommand = nil
            handleMicrophoneControlResult(command, error: nil)
            pumpGattWrites()
        }
    }

    private func pumpGattWrites() {
        sendNextMicrophoneControl()
        if microphoneControlWriteInFlight || !microphoneControlQueue.isEmpty {
            return
        }
        sendNextStatusWrite()
        if statusWriteInFlight || !statusWriteQueue.isEmpty {
            return
        }
        sendNextPanelChunk()
    }

    private func handleMicrophoneControlResult(_ command: MicrophoneControlCommand?, error: Error?) {
        guard let command else { return }
        if let error {
            if command == .armOnDemand {
                microphoneOnDemandSupported = false
                BridgeStatusCenter.shared.microphoneStatus = "Streaming (legacy firmware)"
                log("on-demand microphone unsupported; falling back to continuous stream: \(error.localizedDescription)")
                queueMicrophoneControl(.continuous)
                return
            }
            BridgeStatusCenter.shared.microphoneStatus = "Control failed"
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            sendBridgeHeartbeat()
            return
        }
        switch command {
        case .disable:
            BridgeStatusCenter.shared.microphoneStatus = "Off"
        case .continuous:
            BridgeStatusCenter.shared.microphoneStatus = "Streaming (legacy firmware)"
        case .armOnDemand:
            microphoneOnDemandSupported = true
            BridgeStatusCenter.shared.microphoneStatus = microphoneSessionFaulted ? "Interrupted" : "Ready"
        case .startVoice:
            BridgeStatusCenter.shared.microphoneStatus = "Streaming"
        case .stopVoice:
            BridgeStatusCenter.shared.microphoneStatus = "Ready"
        }
        sendBridgeHeartbeat()
    }

    private func startHealthLoop() {
        guard !options.once, healthTimer == nil else { return }
        healthTimer = Timer.scheduledTimer(withTimeInterval: healthCheckInterval, repeats: true) { [weak self] _ in
            self?.runHealthCheck()
        }
    }

    private func runHealthCheck() {
        guard !options.once, central.state == .poweredOn else { return }
        BridgeStatusCenter.shared.selfCheckStatus = runBridgeSelfCheck().summary
        guard let peripheral else {
            recoverConnection(reason: "health check")
            return
        }
        guard peripheral.state == .connected else { return }

        let missingBridge = eventCharacteristic == nil ||
            statusCharacteristic == nil ||
            !eventNotifyEnabled
        let microphoneEnabled = SettingsStore.shared.settings.virtualMicrophoneEnabled
        let missingMicrophone = microphoneEnabled && (
            microphoneControlCharacteristic == nil ||
            microphoneAudioCharacteristic == nil ||
            microphoneStatsCharacteristic == nil
        )
        if (missingBridge || missingMicrophone) && Date().timeIntervalSince(lastRediscoverAt) > 8 {
            log("Bridge health check: rediscovering services/characteristics bridge=\(missingBridge) microphone=\(missingMicrophone).")
            discoverBridgeServices(peripheral, reason: "health")
        }

        if eventCharacteristic != nil && !eventNotifyEnabled {
            peripheral.setNotifyValue(true, for: eventCharacteristic!)
        }
        for characteristic in hidNotifyCharacteristics where !characteristic.isNotifying {
            peripheral.setNotifyValue(true, for: characteristic)
        }
        if statusCharacteristic != nil {
            sendBridgeHeartbeat()
            refreshCodexUnreadStatus(force: false)
            if !AXIsProcessTrusted() {
                sendBridgeLimited()
            }
        }
        if panelCharacteristic != nil && quotaTimer == nil {
            pushTimePanel(reason: "health")
            startQuotaLoop()
        }
        if statusCharacteristic != nil && pollTimer == nil {
            startStatusLoop()
        }
        if microphoneEnabled {
            runMicrophoneHealthCheck(peripheral, now: Date())
        }
    }

    private func runMicrophoneHealthCheck(_ peripheral: CBPeripheral, now: Date) {
        guard let audio = microphoneAudioCharacteristic else {
            BridgeStatusCenter.shared.microphoneStatus = "Waiting for BLE"
            return
        }

        let health = microphonePipeline.health(now: now)
        checkMicrophoneSessionFault(health, now: now)
        // Payload deduplication makes this free unless readiness changed.
        sendBridgeHeartbeat()
        if now.timeIntervalSince(lastMicrophoneHealthLogAt) >= 30 {
            lastMicrophoneHealthLogAt = now
            let packetAge = health.packetAge.map { String(format: "%.2f", $0) } ?? "--"
            let renderAge = health.renderAge.map { String(format: "%.2f", $0) } ?? "--"
            log(String(format: "microphone health engine=%@ output=%@ render_age=%@ packet_age=%@ packets=%llu lost=%llu rms=%.5f device_stream=%@ subscribed=%@ sent=%u dropped=%u",
                       health.engineRunning ? "running" : "stopped",
                       health.outputHealthy ? "healthy" : "stalled",
                       renderAge,
                       packetAge,
                       health.packetCount,
                       health.lostPacketCount,
                       health.decodedRMS,
                       health.deviceStreaming ? "true" : "false",
                       health.deviceAudioSubscribed ? "true" : "false",
                       health.devicePacketsSent,
                       health.devicePacketsDropped))
            log("microphone output route valid=\(health.outputRouteValid) \(health.outputRouteDescription)")
        }

        if !health.outputHealthy,
           now.timeIntervalSince(lastMicrophoneRecoveryAt) >= 4 {
            lastMicrophoneRecoveryAt = now
            BridgeStatusCenter.shared.microphoneStatus = "Recovering output"
            do {
                let name = health.outputConfigured
                    ? try microphonePipeline.restartOutput()
                    : try microphonePipeline.start()
                BridgeStatusCenter.shared.microphoneStatus = microphoneSessionFaulted
                    ? "Interrupted - press A" : (name == stopWatchVirtualMicrophoneName ? "Ready" : "Ready (BlackHole fallback)")
                log("microphone output recovered: pinned output rebuilt device=\(name) \(microphonePipeline.health().outputRouteDescription)")
            } catch {
                BridgeStatusCenter.shared.microphoneStatus = "Output failed"
                BridgeStatusCenter.shared.lastError = error.localizedDescription
                log("microphone output recovery failed: \(error.localizedDescription)")
            }
            return
        }

        let streamAge = health.packetAge ?? health.outputAge ?? 0
        let statsFresh = health.statsAge.map { $0 <= 3.0 } ?? false
        if !microphoneSessionFaulted,
           health.outputHealthy,
           statsFresh,
           health.deviceStreaming,
           streamAge > 3.0,
           now.timeIntervalSince(lastMicrophoneRecoveryAt) >= 4 {
            lastMicrophoneRecoveryAt = now
            BridgeStatusCenter.shared.microphoneStatus = "Recovering stream"
            if audio.isNotifying {
                microphoneResubscribePending = true
                peripheral.setNotifyValue(false, for: audio)
                log("microphone stream stalled: cycling audio notification subscription")
            } else {
                peripheral.setNotifyValue(true, for: audio)
                log("microphone stream stalled: restoring audio notification subscription")
            }
            return
        }

        if !audio.isNotifying && !microphoneResubscribePending {
            sendBridgeHeartbeat()
            peripheral.setNotifyValue(true, for: audio)
        } else if health.outputHealthy && (health.packetAge.map { $0 <= 3.0 } ?? false) {
            BridgeStatusCenter.shared.microphoneStatus = "Streaming"
        }
    }

    private func sendBridgeHeartbeat(force: Bool = false) {
        guard statusCharacteristic != nil,
              let payload = Optional(bridgeReadyPayload()),
              payload.data(using: .utf8) != nil else {
            return
        }
        guard force || payload != lastBridgeConfigPayload else {
            return
        }
        lastBridgeConfigPayload = payload
        queueStatusWrite(payload, label: "config")
        log("bridge config synced: \(payload)")
    }

    private func bridgeReadyPayload() -> String {
        let settings = SettingsStore.shared.settings
        let mode = settings.inputMode == .wechatIME ? "wechat_ime" : "typeless"
        let primary = hidReportBinding(settings.leftKey)
        let confirm = hidReportBinding(settings.rightKey)
        let confirmLong = hidActionBinding(settings.confirmLongActionName, customKey: settings.confirmLongKey)
        let shake = settings.shakeKey.map(hidReportBinding) ?? (modifier: UInt8(0), keycode: UInt8(0))
        let microphoneReady = microphoneReadyForVoice()
        let payload: [String: Any] = [
            "type": "bridge_ready",
            "version": 1,
            "input_mode": mode,
            "primary_modifier": Int(primary.modifier),
            "primary_key": Int(primary.keycode),
            "confirm_modifier": Int(confirm.modifier),
            "confirm_key": Int(confirm.keycode),
            "confirm_long_action": settings.confirmLongActionName,
            "confirm_long_modifier": Int(confirmLong.modifier),
            "confirm_long_key": Int(confirmLong.keycode),
            "shake_action": settings.shakeActionName,
            "shake_modifier": Int(shake.modifier),
            "shake_key": Int(shake.keycode),
            "microphone_gate_enabled": settings.virtualMicrophoneEnabled,
            "microphone_ready": microphoneReady
        ]
        if let data = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]),
           let text = String(data: data, encoding: .utf8) {
            return text
        }
        let gateEnabled = settings.virtualMicrophoneEnabled ? "true" : "false"
        let ready = microphoneReady ? "true" : "false"
        return "{\"type\":\"bridge_ready\",\"version\":1,\"input_mode\":\"\(mode)\",\"microphone_gate_enabled\":\(gateEnabled),\"microphone_ready\":\(ready)}"
    }

    private func microphoneReadyForVoice() -> Bool {
        let health = microphonePipeline.health()
        return MicrophoneReadinessSnapshot(
            enabled: SettingsStore.shared.settings.virtualMicrophoneEnabled,
            hasControl: microphoneControlCharacteristic != nil,
            audioSubscribed: microphoneAudioCharacteristic?.isNotifying == true,
            statsSubscribed: microphoneStatsCharacteristic?.isNotifying == true,
            armed: microphoneOnDemandSupported == true,
            outputHealthy: health.outputHealthy,
            outputRouteValid: health.outputRouteValid
        ).ready
    }

    private func sendBridgeLimited() {
        let payload = "{\"type\":\"bridge_limited\",\"version\":1,\"helper\":\"limited\"}"
        guard statusCharacteristic != nil,
              payload.data(using: .utf8) != nil else {
            return
        }
        queueStatusWrite(payload, label: "limited")
    }

    private func refreshCodexUnreadStatus(force: Bool) {
        guard statusCharacteristic != nil,
              let count = currentCodexUnreadTaskCount(),
              let tasks = currentCodexUnreadTasks() else {
            return
        }
        let normalizedCount = min(max(count, 0), 999)
        guard force || normalizedCount != lastSentCodexUnreadCount || tasks != lastSentCodexTasks else {
            return
        }
        lastSentCodexUnreadCount = normalizedCount
        lastSentCodexTasks = tasks
        queueStatusWrite("{\"type\":\"codex_unread\",\"version\":1,\"count\":\(normalizedCount)}",
                         label: "codex unread")

        let seq = codexTasksSequence
        codexTasksSequence += 1
        queueStatusWrite("{\"type\":\"codex_tasks\",\"version\":1,\"seq\":\(seq),\"count\":\(tasks.count)}",
                         label: "codex tasks header")
        for (index, task) in tasks.enumerated() {
            let object: [String: Any] = [
                "type": "codex_task",
                "version": 1,
                "seq": seq,
                "index": index,
                "id": task.id,
                "title": String(task.title.prefix(48))
            ]
            guard let data = try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys]),
                  let payload = String(data: data, encoding: .utf8) else {
                continue
            }
            queueStatusWrite(payload, label: "codex tasks item")
        }
        log("Codex unread state synced: count=\(normalizedCount) tasks=\(tasks.count)")
    }

    private func queueStatusWrite(_ payload: String, label: String) {
        guard payload.data(using: .utf8) != nil else { return }
        if label.hasPrefix("voice ") {
            statusWriteQueue.removeAll { $0.label.hasPrefix("voice ") }
        } else if label == "codex unread" {
            statusWriteQueue.removeAll { $0.label == "codex unread" }
        } else if label == "codex tasks header" {
            statusWriteQueue.removeAll { $0.label.hasPrefix("codex tasks") }
        }
        statusWriteQueue.append((payload: payload, label: label))
        pumpGattWrites()
    }

    private func sendNextStatusWrite() {
        guard !statusWriteInFlight,
              !microphoneControlWriteInFlight,
              microphoneControlQueue.isEmpty,
              !panelWriteInFlight,
              let peripheral,
              let characteristic = statusCharacteristic,
              !statusWriteQueue.isEmpty else {
            return
        }

        let item = statusWriteQueue.removeFirst()
        guard let data = item.payload.data(using: .utf8) else {
            pumpGattWrites()
            return
        }

        if characteristic.properties.contains(.write) {
            statusWriteInFlight = true
            peripheral.writeValue(data, for: characteristic, type: .withResponse)
        } else {
            peripheral.writeValue(data, for: characteristic, type: .withoutResponse)
            log("status write sent without response: \(item.label)")
            pumpGattWrites()
        }
    }

    private func handlePrimaryInputDown() {
        OpenWatcherHomeActivityTracker.shared.record("primary_down", weight: 0.7)
        switch SettingsStore.shared.settings.inputMode {
        case .typeless:
            let now = Date()
            if typelessPrimaryIsDown {
                log("typeless primary down ignored; key already down")
                return
            }
            if let lastDown = typelessPrimaryDownAt,
               now.timeIntervalSince(lastDown) < typelessPrimaryDownDebounceSeconds {
                log("typeless primary down ignored; duplicate event debounce")
                return
            }
            typelessPrimaryDownAt = now
            typelessPrimaryIsDown = true
            handleTypelessPrimaryDownStatus()
        case .wechatIME:
            handleWeChatOptionDown()
        }
    }

    private func handlePrimaryInputUp() {
        OpenWatcherHomeActivityTracker.shared.record("primary_up", weight: 0.6)
        switch SettingsStore.shared.settings.inputMode {
        case .typeless:
            guard typelessPrimaryIsDown else {
                log("typeless primary up observed; no active key press")
                return
            }
            typelessPrimaryIsDown = false
            log("typeless primary up observed; ignored in toggle mode, firmware owns HID release")
        case .wechatIME:
            handleWeChatOptionUp()
        }
    }

    private func handleWeChatOptionDown() {
        guard !wechatOptionDown else { return }
        let binding = SettingsStore.shared.settings.leftKey
        wechatOptionDown = true
        wechatHeldBinding = binding
        write(VoiceState(active: true, phase: "recording", message: "WeChat input"))
        log("wechat input: \(binding.name) down delegated to firmware HID keyCode=\(binding.macKeyCode)")
    }

    private func handleWeChatOptionUp() {
        guard wechatOptionDown else { return }
        let binding = wechatHeldBinding ?? SettingsStore.shared.settings.leftKey
        wechatOptionDown = false
        wechatHeldBinding = nil
        write(VoiceState(active: false, phase: "idle", message: "WeChat idle"))
        log("wechat input: \(binding.name) up delegated to firmware HID keyCode=\(binding.macKeyCode)")
    }

    private func handleCodexEnterRequest() {
        OpenWatcherHomeActivityTracker.shared.record("enter", weight: 0.5)
        if SettingsStore.shared.settings.inputMode == .wechatIME {
            if wechatOptionDown {
                handleWeChatOptionUp()
            }
            log("wechat input confirm observed; firmware sent HID")
            return
        }

        if processingUntil != nil || lastState?.phase == "processing" {
            resetTypelessSessionTracking(clearFocus: true)
            write(VoiceState(active: false, phase: "idle", message: ""))
            log("codex enter observed while Typeless processing; cleared device processing state")
            return
        }
        resetTypelessSessionTracking(clearFocus: false)
        write(VoiceState(active: false, phase: "idle", message: ""))
        log("codex enter observed; firmware sent HID")
    }

    private func handleShakeActionRequest() {
        OpenWatcherHomeActivityTracker.shared.record("shake", weight: 0.75)
        if SettingsStore.shared.settings.shakeActionName == "Clear Input" {
            resetTypelessSessionTracking(clearFocus: true)
            write(VoiceState(active: false, phase: "idle", message: ""))
        }
        log("shake action observed; firmware sent HID action \(SettingsStore.shared.settings.shakeActionName)")
    }

    private func resetTypelessSessionTracking(clearFocus: Bool) {
        typelessSessionActive = false
        typelessPrimaryIsDown = false
        processingUntil = nil
        processingStartedAt = nil
        typelessPrimaryDownAt = nil
    }

    private func handleTypelessPrimaryDownStatus() {
        if microphoneSessionFaulted {
            clearMicrophoneSessionFaultForRetry(source: "StopWatch A")
        }
        if processingUntil != nil || lastState?.phase == "processing" {
            typelessSessionActive = false
            processingUntil = nil
            processingStartedAt = nil
            write(VoiceState(active: false, phase: "idle", message: ""))
            log("typeless primary observed while processing; restarting recording")
        }

        let shouldStop = typelessSessionActive ||
            lastState?.phase == "recording"

        if !shouldStop && !prepareMicrophoneForTypelessRecording(source: "StopWatch A") { return }

        if !shouldStop && !isTypelessRunning() {
            resetTypelessSessionTracking(clearFocus: true)
            write(VoiceState(active: false, phase: "idle", message: "正在启动 Typeless"))
            launchTypelessAndReplayPrimaryShortcut()
            return
        }

        if !shouldStop {
            beginTypelessRecordingStatus(source: "device shortcut")
            return
        }

        handleTypelessStopStatus(reason: "toggle")
    }

    @discardableResult
    private func prepareMicrophoneForTypelessRecording(source: String) -> Bool {
        guard SettingsStore.shared.settings.virtualMicrophoneEnabled else { return true }

        if let peripheral,
           let audio = microphoneAudioCharacteristic,
           !audio.isNotifying {
            microphoneResubscribePending = false
            peripheral.setNotifyValue(true, for: audio)
            log("microphone recording preflight restored audio subscription source=\(source)")
        }

        do {
            let result = try microphonePipeline.prepareForRecording(
                rebuildAfterIdle: microphoneOutputPreflightIdleSeconds
            )
            if result.rebuilt {
                log("microphone recording preflight rebuilt output device=\(result.name) source=\(source) \(microphonePipeline.health().outputRouteDescription)")
            }
        } catch {
            BridgeStatusCenter.shared.microphoneStatus = "Output failed"
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("microphone recording preflight failed source=\(source): \(error.localizedDescription)")
            queueMicrophoneControl(.stopVoice)
            triggerMicrophoneSessionFault(reason: "virtual output unavailable: \(error.localizedDescription)",
                                          stopTypeless: currentTypelessState().phase == "recording")
            return false
        }

        queueMicrophoneControl(.startVoice)
        return true
    }

    private func beginTypelessRecordingStatus(source: String) {
        let target = currentInputFocusTarget()
        let focus = target?.focus ?? frontmostFocusSnapshot()
        typelessSessionActive = true
        write(VoiceState(active: true, phase: "recording", message: "正在录制中"))
        if let target {
            log("typeless start observed source=\(source) target=\(target.focus.appName) role=\(target.role)")
        } else if let focus {
            log("typeless start observed source=\(source) target=\(focus.appName) window=\(focus.windowTitle)")
        } else {
            log("typeless start observed source=\(source) without focus snapshot")
        }
    }

    private func launchTypelessAndReplayPrimaryShortcut() {
        guard !typelessLaunchInFlight else {
            log("Typeless launch already in flight; keeping the pending primary shortcut")
            return
        }
        guard let appURL = typelessApplicationURL() else {
            write(VoiceState(active: false, phase: "idle", message: "Typeless 未安装"))
            BridgeStatusCenter.shared.lastError = "Typeless is not installed"
            log("Typeless auto-launch failed: application is not installed")
            return
        }

        typelessLaunchInFlight = true
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.activates = false
        configuration.addsToRecentItems = false
        log("Typeless is not running; launching it before replaying the primary shortcut")
        NSWorkspace.shared.openApplication(at: appURL, configuration: configuration) { [weak self] _, error in
            DispatchQueue.main.async {
                guard let self else { return }
                if let error {
                    self.typelessLaunchInFlight = false
                    self.write(VoiceState(active: false, phase: "idle", message: "Typeless 启动失败"))
                    BridgeStatusCenter.shared.lastError = error.localizedDescription
                    log("Typeless auto-launch failed: \(error.localizedDescription)")
                    return
                }
                self.waitForTypelessShortcutRegistration(
                    deadline: Date().addingTimeInterval(typelessLaunchTimeoutSeconds)
                )
            }
        }
    }

    private func waitForTypelessShortcutRegistration(deadline: Date) {
        if let application = runningTypelessApplication(), application.isFinishedLaunching {
            DispatchQueue.main.asyncAfter(deadline: .now() + typelessShortcutRegistrationDelaySeconds) { [weak self] in
                guard let self, self.typelessLaunchInFlight else { return }
                self.typelessLaunchInFlight = false
                guard self.prepareMicrophoneForTypelessRecording(source: "Typeless auto-launch") else { return }
                self.beginTypelessRecordingStatus(source: "Typeless auto-launch retry")
                if !self.postTypelessPrimaryShortcut(reason: "auto-launch retry", suppressMicrophoneWake: true) {
                    self.resetTypelessSessionTracking(clearFocus: true)
                    self.write(VoiceState(active: false, phase: "idle", message: "Typeless 快捷键失败"))
                }
            }
            return
        }

        guard Date() < deadline else {
            typelessLaunchInFlight = false
            write(VoiceState(active: false, phase: "idle", message: "Typeless 启动超时"))
            BridgeStatusCenter.shared.lastError = "Typeless launch timed out"
            log("Typeless auto-launch timed out before shortcut registration")
            return
        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { [weak self] in
            self?.waitForTypelessShortcutRegistration(deadline: deadline)
        }
    }

    private func handleTypelessStopStatus(reason: String) {
        let canStop = typelessSessionActive ||
            lastState?.phase == "recording"
        guard canStop else {
            log("typeless stop status ignored: no active session reason=\(reason)")
            return
        }

        typelessSessionActive = false
        processingStartedAt = Date()
        processingUntil = Date().addingTimeInterval(typelessProcessingMaximumSeconds)

        write(VoiceState(active: true, phase: "processing", message: ""))
        scheduleProcessingFollowUps()
        log("typeless stop observed reason=\(reason)")
    }

    private func scheduleProcessingFollowUps() {
        for delay in [0.35, 0.9, 1.6, 2.3] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                self?.writeCurrentState(force: false)
            }
        }
        for delay in [3.2, 4.2] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                self?.writeCurrentState(force: true)
            }
        }
    }

    private func startStatusLoop() {
        ensureAccessibilityPrompt(reason: "status loop")
        if AXIsProcessTrusted() {
            writeCurrentState(force: true)
        } else {
            sendBridgeLimited()
            log("Accessibility unavailable; firmware HID fallback remains active.")
        }
        if options.once {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                exit(0)
            }
            return
        }
        pollTimer?.invalidate()
        pollTimer = Timer.scheduledTimer(withTimeInterval: options.interval, repeats: true) { [weak self] _ in
            self?.ensureAccessibilityPrompt(reason: "poll")
            guard AXIsProcessTrusted() else {
                self?.sendBridgeLimited()
                return
            }
            self?.writeCurrentState(force: false)
        }
    }

    private func startQuotaLoop() {
        guard !options.once else {
            return
        }
        guard SettingsStore.shared.settings.enableCodexQuota else {
            BridgeStatusCenter.shared.quotaStatus = "Disabled"
            return
        }
        pushQuotaPanel(reason: "connect")
        quotaTimer?.invalidate()
        let interval = CloudUsageSynchronizer.shared.configured ? 60.0 : TimeInterval(SettingsStore.shared.settings.quotaRefreshSeconds)
        quotaTimer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { [weak self] _ in
            self?.pushQuotaPanel(reason: "quota")
        }
    }

    private func pushTimePanel(reason: String) {
        guard panelCharacteristic != nil else {
            return
        }
        let now = Date()
        guard now.timeIntervalSince(lastTimePanelPushAt) > 15 else {
            return
        }
        lastTimePanelPushAt = now

        do {
            let panel = try buildDeviceTimePanel()
            sendPanelData(panel,
                          kind: "time",
                          finalStatus: "Time \(DateFormatter.localizedString(from: now, dateStyle: .none, timeStyle: .medium))",
                          reason: reason)
        } catch {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("time panel push failed: \(error.localizedDescription)")
        }
    }

    private func pushQuotaPanel(reason: String = "quota") {
        guard SettingsStore.shared.settings.enableCodexQuota,
              panelCharacteristic != nil || CloudUsageSynchronizer.shared.configured else {
            return
        }
        guard !quotaFetchInFlight else {
            return
        }

        quotaFetchInFlight = true
        BridgeStatusCenter.shared.quotaStatus = "Fetching"
        DispatchQueue.global(qos: .utility).async { [weak self] in
            do {
                OpenWatcherHomeActivityTracker.shared.record("quota_sync", weight: 0.45)
                let panel = try buildDevicePanel()
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.quotaFetchInFlight = false
                    // Cloud refreshes every minute; BLE display cadence remains
                    // unchanged. Never add a panel burst during dictation.
                    guard self.panelCharacteristic != nil,
                          self.lastState?.phase != "recording", !self.typelessSessionActive,
                          reason != "quota" || Date().timeIntervalSince(self.lastQuotaPanelSentAt) >= TimeInterval(SettingsStore.shared.settings.quotaRefreshSeconds) else {
                        BridgeStatusCenter.shared.quotaStatus = "Cloud synced"
                        return
                    }
                    self.lastQuotaPanelSentAt = Date()
                    self.sendPanelData(panel,
                                       kind: "quota",
                                       finalStatus: "Synced \(DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium))",
                                       reason: reason)
                }
            } catch {
                DispatchQueue.main.async {
                    self?.quotaFetchInFlight = false
                    BridgeStatusCenter.shared.quotaStatus = "Failed"
                    BridgeStatusCenter.shared.lastError = error.localizedDescription
                    log("quota panel push failed: \(error.localizedDescription)")
                }
            }
        }
    }

    private func sendPanelData(_ panel: Data, kind: String, finalStatus: String, reason: String) {
        guard let characteristic = panelCharacteristic else {
            return
        }

        do {
            let bytes = [UInt8](panel)
            let chunkSize = 48
            let parts = max(1, Int(ceil(Double(bytes.count) / Double(chunkSize))))
            let seq = panelSequence
            panelSequence += 1
            var chunks: [Data] = []

            for index in 0..<parts {
                let start = index * chunkSize
                let end = min(bytes.count, start + chunkSize)
                let chunkData = Data(bytes[start..<end])
                let envelope: [String: Any] = [
                    "version": 1,
                    "type": "panel_chunk",
                    "seq": seq,
                    "part": index + 1,
                    "parts": parts,
                    "data": hexString(chunkData)
                ]
                let data = try JSONSerialization.data(withJSONObject: envelope, options: [])
                chunks.append(data)
            }
            panelTransfers.append(PendingPanelTransfer(chunks: chunks,
                                                       seq: seq,
                                                       bytes: bytes.count,
                                                       kind: kind,
                                                       finalStatus: finalStatus,
                                                       reason: reason,
                                                       characteristic: characteristic))
            pumpGattWrites()
        } catch {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("\(kind) panel encode failed: \(error.localizedDescription)")
        }
    }

    private func sendNextPanelChunk() {
        guard !panelWriteInFlight,
              !microphoneControlWriteInFlight,
              microphoneControlQueue.isEmpty,
              !statusWriteInFlight,
              statusWriteQueue.isEmpty,
              let peripheral else {
            return
        }

        while !panelTransfers.isEmpty,
              panelTransfers[0].index >= panelTransfers[0].chunks.count {
            let completed = panelTransfers.removeFirst()
            if completed.kind == "quota" || completed.kind == "time" {
                BridgeStatusCenter.shared.quotaStatus = completed.finalStatus
            }
            log("\(completed.kind) panel pushed seq=\(completed.seq) bytes=\(completed.bytes) parts=\(completed.chunks.count) reason=\(completed.reason)")
        }
        guard !panelTransfers.isEmpty else { return }

        let transfer = panelTransfers[0]
        let chunk = transfer.chunks[transfer.index]
        panelTransfers[0].index += 1
        let writeType: CBCharacteristicWriteType = transfer.characteristic.properties.contains(.write)
            ? .withResponse : .withoutResponse
        panelWriteInFlight = writeType == .withResponse
        peripheral.writeValue(chunk, for: transfer.characteristic, type: writeType)
        if writeType == .withoutResponse {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.03) { [weak self] in
                self?.pumpGattWrites()
            }
        }
    }

    private func writeCurrentState(force: Bool) {
        checkMicrophoneSessionFault(microphonePipeline.health(now: Date()), now: Date())
        let state = resolvedState()
        if state.phase == "idle" && !typelessPrimaryIsDown {
            typelessSessionActive = false
        }
        guard force || state != lastState else {
            return
        }
        write(state)
        lastState = state
    }

    private func resolvedState() -> VoiceState {
        if microphoneSessionFaulted {
            return VoiceState(active: false, phase: "error", message: "录音中断，按 A 重试")
        }
        if let forcedState = options.forcedState {
            return forcedState
        }
        if let status = options.status {
            let lower = status.lowercased()
            let processing = lower.contains("处理") || lower.contains("processing")
            let active = processing || lower.contains("录制") || lower.contains("recording") || lower.contains("listening")
            return VoiceState(active: active, phase: processing ? "processing" : (active ? "recording" : "idle"), message: status)
        }
        if let until = processingUntil {
            let detected = currentTypelessState()
            let idleGraceUntil = processingStartedAt?.addingTimeInterval(typelessProcessingIdleGraceSeconds) ?? Date.distantPast
            if Date() >= until {
                processingUntil = nil
                processingStartedAt = nil
                if detected.phase == "recording" {
                    return detected
                }
                return VoiceState(active: false, phase: "idle", message: "")
            }
            if detected.phase == "idle" && detected.message == "Typeless sent" {
                processingUntil = nil
                processingStartedAt = nil
                return detected
            }
            if detected.phase == "idle" && Date() >= idleGraceUntil {
                processingUntil = nil
                processingStartedAt = nil
                if detected.message == "Typeless 待机" {
                    return VoiceState(active: false, phase: "idle", message: "")
                }
                return detected
            }
            if detected.phase == "recording" {
                processingUntil = nil
                processingStartedAt = nil
                return detected
            }
            if detected.phase == "processing" {
                return detected
            }
            return VoiceState(active: true, phase: "processing", message: "")
        }
        let detected = currentTypelessState()
        if typelessPrimaryIsDown || typelessSessionActive {
            return VoiceState(active: true, phase: "recording", message: "正在录制中")
        }
        return detected
    }

    private func write(_ state: VoiceState) {
        guard statusCharacteristic != nil,
              state.payload.data(using: .utf8) != nil else {
            return
        }
        let previousPhase = lastState?.phase
        queueStatusWrite(state.payload, label: "voice \(state.phase)")
        lastState = state
        BridgeStatusCenter.shared.typelessStatus = state.message.isEmpty ? state.phase : state.message
        switch state.phase {
        case "recording":
            if previousPhase != "recording" {
                microphoneRecordingStartedAt = Date()
                microphoneDroppedBaseline = microphonePipeline.health(now: Date()).devicePacketsDropped
                microphoneDroppedBaselineAt = Date()
            }
            queueMicrophoneControl(.startVoice)
            OpenWatcherHomeActivityTracker.shared.record("recording", weight: 0.85)
        case "processing":
            microphoneRecordingStartedAt = nil
            microphoneDroppedBaselineAt = nil
            queueMicrophoneControl(.stopVoice)
            OpenWatcherHomeActivityTracker.shared.record("processing", weight: 0.9)
        default:
            microphoneRecordingStartedAt = nil
            microphoneDroppedBaselineAt = nil
            queueMicrophoneControl(.stopVoice)
            OpenWatcherHomeActivityTracker.shared.record("idle", weight: 0.15)
        }
        log("state \(state.phase) active=\(state.active) message=\(state.message)")
    }

    private func checkMicrophoneSessionFault(_ health: MicrophonePipelineHealth, now: Date) {
        if !microphoneSessionFaulted,
           SettingsStore.shared.settings.virtualMicrophoneEnabled,
           SettingsStore.shared.settings.inputMode == .typeless,
           (typelessSessionActive || lastState?.phase == "recording"),
           !health.outputRouteValid {
            triggerMicrophoneSessionFault(reason: "virtual output route lost: \(health.outputRouteDescription)",
                                          stopTypeless: true)
            return
        }
        guard !microphoneSessionFaulted,
              SettingsStore.shared.settings.virtualMicrophoneEnabled,
              SettingsStore.shared.settings.inputMode == .typeless,
              typelessSessionActive || lastState?.phase == "recording",
              let startedAt = microphoneRecordingStartedAt,
              now.timeIntervalSince(startedAt) >= microphoneStreamStartGraceSeconds else {
            return
        }

        let statsFresh = health.statsAge.map { $0 <= 2.5 } ?? false
        if statsFresh && !health.deviceStreaming {
            triggerMicrophoneSessionFault(reason: "device audio stream stopped during recording",
                                          stopTypeless: true)
            return
        }
        let packetAge = health.packetAge ?? now.timeIntervalSince(startedAt)
        if statsFresh,
           health.deviceStreaming,
           packetAge >= microphoneStreamFaultSeconds {
            triggerMicrophoneSessionFault(reason: String(format: "audio packets stalled for %.2fs", packetAge),
                                          stopTypeless: true,
                                          recoverTransport: true)
            return
        }
        if health.latestPacketGapCount >= microphoneFatalPacketGap {
            triggerMicrophoneSessionFault(reason: "audio packet gap reached \(health.latestPacketGapCount) frames",
                                          stopTypeless: true,
                                          recoverTransport: true)
            return
        }
        if health.devicePacketsDropped < microphoneDroppedBaseline {
            // Firmware counters restart at the beginning of each voice
            // generation. Rebase before calculating this session's delta.
            microphoneDroppedBaseline = health.devicePacketsDropped
            microphoneDroppedBaselineAt = now
            return
        }
        if health.devicePacketsDropped - microphoneDroppedBaseline >= microphoneFatalDroppedFrames {
            triggerMicrophoneSessionFault(reason: "device dropped \(health.devicePacketsDropped - microphoneDroppedBaseline) frames",
                                          stopTypeless: true,
                                          recoverTransport: true)
            return
        }
        if let baselineAt = microphoneDroppedBaselineAt,
           now.timeIntervalSince(baselineAt) > 2.0 {
            microphoneDroppedBaseline = health.devicePacketsDropped
            microphoneDroppedBaselineAt = now
        }
    }

    private func triggerMicrophoneSessionFault(reason: String,
                                               stopTypeless: Bool,
                                               recoverTransport: Bool = false) {
        guard !microphoneSessionFaulted else { return }
        microphoneSessionFaulted = true
        microphoneRecordingStartedAt = nil
        microphoneDroppedBaselineAt = nil
        typelessSessionActive = false
        typelessPrimaryIsDown = false
        processingUntil = nil
        processingStartedAt = nil
        microphonePipeline.prepareForStreamRestart()
        let faultState = VoiceState(active: false, phase: "error", message: "录音中断，按 A 重试")
        lastState = faultState
        BridgeStatusCenter.shared.microphoneStatus = "Interrupted"
        BridgeStatusCenter.shared.typelessStatus = faultState.message
        BridgeStatusCenter.shared.lastError = reason
        if stopTypeless && isTypelessRunning() {
            _ = postTypelessPrimaryShortcut(reason: "microphone fault stop", suppressMicrophoneWake: true)
        }
        if statusCharacteristic != nil {
            write(faultState)
        }
        if recoverTransport {
            scheduleMicrophoneTransportRecovery(reason: reason)
        }
        NSApp.requestUserAttention(.criticalRequest)
        log("microphone session fault: \(reason); automatic resume disabled")
    }

    private func scheduleMicrophoneTransportRecovery(reason: String) {
        microphoneTransportRecoveryWorkItem?.cancel()
        let now = Date()
        let repeatedFault = lastMicrophoneTransportFaultAt.map {
            now.timeIntervalSince($0) <= 60
        } ?? false
        lastMicrophoneTransportFaultAt = now

        let work = DispatchWorkItem { [weak self] in
            guard let self,
                  let peripheral = self.peripheral,
                  peripheral.state == .connected else { return }
            self.microphoneTransportRecoveryWorkItem = nil

            if repeatedFault {
                self.microphoneResubscribePending = false
                BridgeStatusCenter.shared.microphoneStatus = "Reconnecting BLE"
                log("microphone transport fault repeated; rebuilding BLE connection reason=\(reason)")
                self.central.cancelPeripheralConnection(peripheral)
                return
            }

            guard let audio = self.microphoneAudioCharacteristic else { return }
            self.microphoneResubscribePending = true
            BridgeStatusCenter.shared.microphoneStatus = "Recovering stream"
            if audio.isNotifying {
                peripheral.setNotifyValue(false, for: audio)
                log("microphone transport recovery: cycling audio subscription reason=\(reason)")
            } else {
                peripheral.setNotifyValue(true, for: audio)
                log("microphone transport recovery: restoring audio subscription reason=\(reason)")
            }
        }
        microphoneTransportRecoveryWorkItem = work
        // Let stopVoice and the visible error reach the watch before changing
        // the CCCD or reconnecting the BLE link shared with native HID.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: work)
    }

    private func clearMicrophoneSessionFaultForRetry(source: String) {
        guard microphoneSessionFaulted else { return }
        microphoneSessionFaulted = false
        microphonePipeline.prepareForStreamRestart()
        microphoneDroppedBaseline = 0
        microphoneDroppedBaselineAt = nil
        lastState = VoiceState(active: false, phase: "idle", message: "")
        BridgeStatusCenter.shared.microphoneStatus = "Ready"
        BridgeStatusCenter.shared.typelessStatus = "Ready"
        BridgeStatusCenter.shared.lastError = ""
        log("microphone session fault cleared for retry source=\(source)")
    }

    @discardableResult
    private func postTypelessPrimaryShortcut(reason: String, suppressMicrophoneWake: Bool) -> Bool {
        let binding = SettingsStore.shared.settings.leftKey
        guard let source = CGEventSource(stateID: .hidSystemState),
              let down = CGEvent(keyboardEventSource: source,
                                 virtualKey: CGKeyCode(binding.macKeyCode),
                                 keyDown: true),
              let up = CGEvent(keyboardEventSource: source,
                               virtualKey: CGKeyCode(binding.macKeyCode),
                               keyDown: false) else {
            log("could not synthesize Typeless shortcut reason=\(reason)")
            return false
        }
        if suppressMicrophoneWake {
            suppressNextMicrophoneShortcut = true
        }
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
        if suppressMicrophoneWake {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
                self?.suppressNextMicrophoneShortcut = false
            }
        }
        log("sent Typeless shortcut \(binding.name) reason=\(reason)")
        return true
    }
}

private let options = parseOptions()
if options.requestAccessibility {
    let trusted = requestAccessibilityAccess()
    log(trusted ? "Accessibility already authorized." : "Accessibility authorization requested.")
    exit(trusted ? 0 : 1)
}

if options.checkAccessibility {
    let trusted = AXIsProcessTrusted()
    log("Accessibility trusted=\(trusted)")
    print(trusted ? "trusted" : "not_trusted")
    exit(trusted ? 0 : 1)
}

if options.once || options.forcedState != nil || options.status != nil {
    let bridge = StopWatchBleBridge(options: options)
    withExtendedLifetime(bridge) {
        RunLoop.main.run()
    }
} else {
    let app = NSApplication.shared
    let delegate = BridgeAppDelegate(options: options)
    app.delegate = delegate
    app.setActivationPolicy(.accessory)
    app.run()
}
