import AppKit
import ApplicationServices
import Carbon
import CoreBluetooth
import Darwin
import Foundation

private let deviceNamePrefix = "M5Codex-"
private let legacyDeviceNames: Set<String> = ["M5Codex-HID4", "M5Codex-HID5"]
private let hidServiceUUID = CBUUID(string: "1812")
private let serviceUUID = CBUUID(string: "ABCD0000-E819-B394-6344-2A2F31424C45")
private let eventUUID = CBUUID(string: "ABCD0001-E819-B394-6344-2A2F31424C45")
private let statusUUID = CBUUID(string: "ABCD0002-E819-B394-6344-2A2F31424C45")
private let panelUUID = CBUUID(string: "ABCD0003-E819-B394-6344-2A2F31424C45")
private let healthCheckInterval: TimeInterval = 5
private let codexDeviceId = "m5stack-stopwatch"
private let supportDirectoryURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Application Support/M5StopWatch/StopWatchBleBridge")
private let configFileURL = supportDirectoryURL.appendingPathComponent("config.json")
private let logFileURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Logs/stopwatch-ble-bridge.log")
private let bridgeSettingsChangedNotification = Notification.Name("StopWatchBleBridgeSettingsChanged")

private func isSupportedDeviceName(_ name: String?) -> Bool {
    guard let name else { return false }
    return name.hasPrefix(deviceNamePrefix) || legacyDeviceNames.contains(name)
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

private let availableShakeActions = ["Clear Input", "Escape", "Return", "None"]
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
    var shakeActionName: String = "Clear Input"
    var customKeyBindings: [KeyBinding] = []
    var inputModeName: String = InputMode.typeless.rawValue
    var quotaRefreshSeconds: Int = 300
    var enableCodexQuota: Bool = true
    var syncTypelessShortcut: Bool = false

    private enum CodingKeys: String, CodingKey {
        case leftKeyName
        case rightKeyName
        case typelessLeftKeyName
        case typelessRightKeyName
        case wechatLeftKeyName
        case wechatRightKeyName
        case shakeActionName
        case customKeyBindings
        case inputModeName
        case quotaRefreshSeconds
        case enableCodexQuota
        case syncTypelessShortcut
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
        shakeActionName = try container.decodeIfPresent(String.self, forKey: .shakeActionName) ?? "Clear Input"
        customKeyBindings = try container.decodeIfPresent([KeyBinding].self, forKey: .customKeyBindings) ?? []
        inputModeName = try container.decodeIfPresent(String.self, forKey: .inputModeName) ?? InputMode.typeless.rawValue
        quotaRefreshSeconds = try container.decodeIfPresent(Int.self, forKey: .quotaRefreshSeconds) ?? 300
        enableCodexQuota = try container.decodeIfPresent(Bool.self, forKey: .enableCodexQuota) ?? true
        syncTypelessShortcut = try container.decodeIfPresent(Bool.self, forKey: .syncTypelessShortcut) ?? false
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
    var lastError = "" { didSet { refreshMenu() } }

    weak var appDelegate: BridgeAppDelegate?

    func refreshMenu() {
        DispatchQueue.main.async { [weak self] in
            self?.appDelegate?.refreshMenu()
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
    if NSWorkspace.shared.runningApplications.contains(where: {
        $0.bundleIdentifier == "now.typeless.desktop" || $0.localizedName == "Typeless"
    }) {
        return "Running"
    }
    let appURLs = [
        URL(fileURLWithPath: "/Applications/Typeless.app"),
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications/Typeless.app")
    ]
    return appURLs.contains(where: { FileManager.default.fileExists(atPath: $0.path) }) ? "Installed" : "Optional"
}

private func isTypelessRunning() -> Bool {
    NSWorkspace.shared.runningApplications.contains {
        $0.bundleIdentifier == "now.typeless.desktop" || $0.localizedName == "Typeless"
    }
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

private func postKeyTap(_ binding: KeyBinding) {
    let source = CGEventSource(stateID: .hidSystemState)
    let keyDown = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(binding.macKeyCode), keyDown: true)
    keyDown?.post(tap: .cghidEventTap)

    let keyUp = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(binding.macKeyCode), keyDown: false)
    keyUp?.post(tap: .cghidEventTap)
}

private func postKeyTap(keyCode: UInt16, flags: CGEventFlags = []) {
    let source = CGEventSource(stateID: .hidSystemState)
    let keyDown = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(keyCode), keyDown: true)
    keyDown?.flags = flags
    keyDown?.post(tap: .cghidEventTap)

    let keyUp = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(keyCode), keyDown: false)
    keyUp?.flags = flags
    keyUp?.post(tap: .cghidEventTap)
}

private func postKeyDown(keyCode: UInt16, flags: CGEventFlags = []) {
    let source = CGEventSource(stateID: .hidSystemState)
    let event = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(keyCode), keyDown: true)
    event?.flags = flags
    event?.post(tap: .cghidEventTap)
}

private func postKeyUp(keyCode: UInt16, flags: CGEventFlags = []) {
    let source = CGEventSource(stateID: .hidSystemState)
    let event = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(keyCode), keyDown: false)
    event?.flags = flags
    event?.post(tap: .cghidEventTap)
}

@discardableResult
private func postRightOption(isDown: Bool) -> Bool {
    let event = CGEvent(keyboardEventSource: nil,
                        virtualKey: CGKeyCode(kVK_RightOption),
                        keyDown: isDown)
    guard let event else { return false }
    event.flags = isDown ? .maskAlternate : []
    event.post(tap: .cghidEventTap)
    return true
}

@discardableResult
private func postKeyDown(_ binding: KeyBinding) -> Bool {
    if binding.name == "Right Option" {
        return postRightOption(isDown: true)
    }
    let flags: CGEventFlags = binding.name == "Right Option" ? .maskAlternate : []
    postKeyDown(keyCode: binding.macKeyCode, flags: flags)
    return true
}

@discardableResult
private func postKeyUp(_ binding: KeyBinding) -> Bool {
    if binding.name == "Right Option" {
        return postRightOption(isDown: false)
    }
    postKeyUp(keyCode: binding.macKeyCode, flags: [])
    return true
}

private func hidReportBinding(_ binding: KeyBinding) -> (modifier: UInt8, keycode: UInt8) {
    binding.name == "Right Option" ? (0x40, binding.hidKeyCode) : (0, binding.hidKeyCode)
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

private func restoreFocus(_ snapshot: FocusSnapshot?) -> Bool {
    guard let snapshot else {
        return false
    }

    let candidates = NSWorkspace.shared.runningApplications.filter {
        if let bundleIdentifier = snapshot.bundleIdentifier, $0.bundleIdentifier == bundleIdentifier {
            return true
        }
        return $0.processIdentifier == snapshot.processIdentifier
    }
    guard let app = candidates.first else {
        return false
    }

    let activated = app.activate(options: [.activateIgnoringOtherApps])
    let axApp = AXUIElementCreateApplication(app.processIdentifier)

    if let window = snapshot.window {
        AXUIElementSetAttributeValue(axApp, kAXFocusedWindowAttribute as CFString, window)
        AXUIElementPerformAction(window, kAXRaiseAction as CFString)
    }

    return activated
}

private func restoreInputFocus(_ target: InputFocusTarget?) -> Bool {
    guard let target else {
        return false
    }
    guard restoreFocus(target.focus) else {
        return false
    }

    let axApp = AXUIElementCreateApplication(target.focus.processIdentifier)
    if let window = target.focus.window {
        AXUIElementSetAttributeValue(axApp, kAXFocusedWindowAttribute as CFString, window)
        AXUIElementPerformAction(window, kAXRaiseAction as CFString)
    }

    let focusedOnApp = AXUIElementSetAttributeValue(axApp, kAXFocusedUIElementAttribute as CFString, target.element)
    let focusedOnElement = AXUIElementSetAttributeValue(target.element,
                                                        kAXFocusedAttribute as CFString,
                                                        kCFBooleanTrue)
    return focusedOnApp == .success || focusedOnElement == .success
}

private func restoreInputFocusAfterTypelessShortcut(target: InputFocusTarget?,
                                                    snapshot: FocusSnapshot?,
                                                    reason: String) {
    let restoreAction = {
        let restored: Bool
        if let target {
            restored = restoreInputFocus(target)
            if !restored {
                _ = restoreFocus(target.focus)
            }
        } else {
            restored = restoreFocus(snapshot)
        }
        log(restored ? "focus restored after typeless \(reason)" : "focus restore failed after typeless \(reason)")
    }

    DispatchQueue.main.asyncAfter(deadline: .now() + 0.18, execute: restoreAction)
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.55, execute: restoreAction)
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

private func fetchCodexUsage() throws -> [String: Any] {
    let auth = try codexAuth()
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

private func buildDevicePanel() throws -> Data {
    let raw = try fetchCodexUsage()
    let rateLimit = raw["rate_limit"] as? [String: Any] ?? raw
    var windows: [Int: [String: Any]] = [:]
    for item in flattenDictionaries(rateLimit) {
        guard let seconds = (item["limit_window_seconds"] as? Int) ??
            ((item["limit_window_seconds"] as? String).flatMap { Int($0) })
        else {
            continue
        }
        if seconds == 18000 || seconds == 604800 {
            windows[seconds] = item
        }
    }

    let five = windows[18000] ?? [:]
    let weekly = windows[604800] ?? [:]
    let fiveLeft = leftPercent(from: five)
    let weeklyLeft = leftPercent(from: weekly)
    let valid = fiveLeft != nil && weeklyLeft != nil

    let now = ISO8601DateFormatter().string(from: Date())
    let epoch = Int(Date().timeIntervalSince1970)
    let codex: [String: Any] = [
        "valid": valid,
        "status": valid ? "ok" : "invalid",
        "processing": false,
        "message": valid ? "Quota synced" : "Quota invalid",
        "five_hour": [
            "left_pct": fiveLeft ?? 0,
            "used_pct": 100 - (fiveLeft ?? 0),
            "reset_in": resetText(from: five),
            "reset_at": ""
        ],
        "weekly": [
            "left_pct": weeklyLeft ?? 0,
            "used_pct": 100 - (weeklyLeft ?? 0),
            "reset_in": resetText(from: weekly),
            "reset_at": ""
        ]
    ]
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
            "five_hour": ["left_pct": -1, "reset_in": "--"],
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
    private let shakePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let captureLeftButton = NSButton(title: "录入", target: nil, action: nil)
    private let captureRightButton = NSButton(title: "录入", target: nil, action: nil)
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
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 470),
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
        title.frame = NSRect(x: 28, y: 420, width: 360, height: 26)
        content.addSubview(title)

        let subtitle = NSTextField(labelWithString: "切换输入法模式时，会自动恢复该模式保存过的按键绑定。")
        subtitle.textColor = .secondaryLabelColor
        subtitle.frame = NSRect(x: 28, y: 394, width: 455, height: 20)
        content.addSubview(subtitle)

        let modeBox = makeCard(frame: NSRect(x: 20, y: 294, width: 480, height: 86))
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

        let keyBox = makeCard(frame: NSRect(x: 20, y: 142, width: 480, height: 136))
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

        let behaviorBox = makeCard(frame: NSRect(x: 20, y: 32, width: 480, height: 94))
        content.addSubview(behaviorBox)
        addSectionTitle("同步与动作", x: 20, y: 60, to: behaviorBox)
        addLabel("摇晃动作", x: 20, y: 26, to: behaviorBox)
        populateShakePopup()
        shakePopup.frame = NSRect(x: 154, y: 21, width: 154, height: 30)
        behaviorBox.addSubview(shakePopup)
        captureShakeButton.target = self
        captureShakeButton.action = #selector(captureShakeKey)
        captureShakeButton.bezelStyle = .rounded
        captureShakeButton.frame = NSRect(x: 314, y: 21, width: 58, height: 30)
        behaviorBox.addSubview(captureShakeButton)
        addLabel("额度刷新", x: 384, y: 26, to: behaviorBox)
        quotaField.placeholderString = "300"
        quotaField.alignment = .right
        quotaField.frame = NSRect(x: 432, y: 24, width: 34, height: 24)
        behaviorBox.addSubview(quotaField)
        let seconds = NSTextField(labelWithString: "秒")
        seconds.textColor = .secondaryLabelColor
        seconds.frame = NSRect(x: 468, y: 26, width: 18, height: 20)
        behaviorBox.addSubview(seconds)

        quotaCheckbox.frame = NSRect(x: 28, y: 8, width: 250, height: 22)
        content.addSubview(quotaCheckbox)

        syncTypelessCheckbox.frame = NSRect(x: 278, y: 8, width: 214, height: 22)
        content.addSubview(syncTypelessCheckbox)

        let save = NSButton(title: "保存", target: self, action: #selector(saveSettings))
        save.bezelStyle = .rounded
        save.keyEquivalent = "\r"
        save.frame = NSRect(x: 402, y: 420, width: 86, height: 30)
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
        shakePopup.removeAllItems()
        for action in availableShakeActions {
            shakePopup.addItem(withTitle: shakeActionTitles[action] ?? action)
            shakePopup.lastItem?.representedObject = action
        }
        shakePopup.menu?.addItem(.separator())
        for binding in availableKeyBindings + draftSettings.customKeyBindings {
            shakePopup.addItem(withTitle: "按键：\(displayKeyName(binding.name))")
            shakePopup.lastItem?.representedObject = "Key:\(binding.name)"
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

    private func refreshBindingPopups(keepingLeft left: String?, right: String?, shake: String?) {
        populateKeyPopup(leftPopup)
        populateKeyPopup(rightPopup)
        populateShakePopup()
        if let left {
            selectRaw(left, in: leftPopup)
        }
        if let right {
            selectRaw(right, in: rightPopup)
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
            let shake = self.selectedRaw(in: self.shakePopup, fallback: "Clear Input")
            self.refreshBindingPopups(keepingLeft: binding.name, right: right, shake: shake)
        }
    }

    @objc private func captureRightKey() {
        captureKey(title: "录入 B 键绑定") { [weak self] binding in
            guard let self else { return }
            let left = self.selectedRaw(in: self.leftPopup, fallback: "F19")
            let shake = self.selectedRaw(in: self.shakePopup, fallback: "Clear Input")
            self.refreshBindingPopups(keepingLeft: left, right: binding.name, shake: shake)
        }
    }

    @objc private func captureShakeKey() {
        captureKey(title: "录入摇晃绑定") { [weak self] binding in
            guard let self else { return }
            let left = self.selectedRaw(in: self.leftPopup, fallback: "F19")
            let right = self.selectedRaw(in: self.rightPopup, fallback: "Return")
            self.refreshBindingPopups(keepingLeft: left, right: right, shake: "Key:\(binding.name)")
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
        refreshBindingPopups(keepingLeft: nil, right: nil, shake: nil)
        selectRaw(editingMode.rawValue, in: inputModePopup)
        loadBindingDraft(for: editingMode)
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

private final class StopWatchBleBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private let options: Options
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var eventCharacteristic: CBCharacteristic?
    private var statusCharacteristic: CBCharacteristic?
    private var panelCharacteristic: CBCharacteristic?
    private var eventNotifyEnabled = false
    private var lastState: VoiceState?
    private var pollTimer: Timer?
    private var quotaTimer: Timer?
    private var healthTimer: Timer?
    private var panelSequence = 1
    private var quotaFetchInFlight = false
    private var lastTimePanelPushAt = Date.distantPast
    private var typelessSessionActive = false
    private var typelessFocusSnapshot: FocusSnapshot?
    private var lockedInputTarget: InputFocusTarget?
    private var processingUntil: Date?
    private var pendingCodexEnter = false
    private var wechatOptionDown = false
    private var wechatHeldBinding: KeyBinding?
    private var suppressLegacyPrimaryEventsUntil = Date.distantPast
    private var suppressLegacyConfirmEventsUntil = Date.distantPast
    private var lastRediscoverAt = Date.distantPast
    private var lastBridgeConfigPayload: String?
    private var didPromptAccessibility = false

    init(options: Options) {
        self.options = options
        super.init()
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(settingsChanged),
                                               name: bridgeSettingsChangedNotification,
                                               object: nil)
        ensureAccessibilityPrompt(reason: "startup")
        central = CBCentralManager(delegate: self, queue: .main)
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    @objc private func settingsChanged() {
        SettingsStore.shared.reload()
        if SettingsStore.shared.settings.inputMode != .wechatIME && wechatOptionDown {
            if let binding = wechatHeldBinding {
                postKeyUp(binding)
            } else {
                postKeyUp(keyCode: UInt16(kVK_RightOption), flags: [])
            }
            wechatOptionDown = false
            wechatHeldBinding = nil
        }
        BridgeStatusCenter.shared.selfCheckStatus = runBridgeSelfCheck().summary
        BridgeStatusCenter.shared.inputMode = SettingsStore.shared.settings.inputMode.rawValue
        quotaTimer?.invalidate()
        quotaTimer = nil
        if statusCharacteristic != nil {
            sendBridgeHeartbeat(force: true)
        }
        if panelCharacteristic != nil {
            pushTimePanel(reason: "settings")
            startQuotaLoop()
        }
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
            let connected = central.retrieveConnectedPeripherals(withServices: [serviceUUID]) +
                central.retrieveConnectedPeripherals(withServices: [hidServiceUUID])
            if let peripheral = connected.first(where: { isSupportedDeviceName($0.name) }) {
                self.peripheral = peripheral
                peripheral.delegate = self
                BridgeStatusCenter.shared.bleStatus = "Opening \(peripheral.name ?? deviceNamePrefix)"
                log("Found connected \(peripheral.name ?? deviceNamePrefix). Opening bridge service...")
                central.connect(peripheral)
                return
            }

            BridgeStatusCenter.shared.bleStatus = "Scanning"
            log("BLE powered on. Scanning for \(deviceNamePrefix)* HID service...")
            scanForDevice(hidServiceOnly: true)
            DispatchQueue.main.asyncAfter(deadline: .now() + 4.0) { [weak self] in
                guard let self, self.peripheral == nil else { return }
                log("Service scan did not find device yet. Falling back to name scan...")
                self.scanForDevice(hidServiceOnly: false)
            }
        default:
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

        self.peripheral = peripheral
        peripheral.delegate = self
        central.stopScan()
        BridgeStatusCenter.shared.bleStatus = "Connecting"
        log("Found \(peripheral.name ?? advName ?? deviceNamePrefix), RSSI \(RSSI). Connecting bridge...")
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        resetBridgeCharacteristics()
        BridgeStatusCenter.shared.bleStatus = "Connected"
        log("Connected. Discovering bridge service...")
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        BridgeStatusCenter.shared.bleStatus = "Connect failed"
        BridgeStatusCenter.shared.lastError = error?.localizedDescription ?? "unknown"
        log("Connect failed: \(error?.localizedDescription ?? "unknown")")
        if options.once {
            exit(1)
        }
        scanForDevice(hidServiceOnly: true)
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        BridgeStatusCenter.shared.bleStatus = "Disconnected"
        log("Disconnected: \(error?.localizedDescription ?? "normal")")
        pollTimer?.invalidate()
        pollTimer = nil
        quotaTimer?.invalidate()
        quotaTimer = nil
        resetBridgeCharacteristics()
        self.peripheral = nil
        if options.once {
            exit(0)
        }
        scanForDevice(hidServiceOnly: true)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("Discover services failed: \(error.localizedDescription)")
            exit(1)
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
            BridgeStatusCenter.shared.bleStatus = "Bridge service missing"
            log("Bridge service not found. Check firmware UUID / Bluetooth setting.")
            exit(1)
        }
        peripheral.discoverCharacteristics([eventUUID, statusUUID, panelUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let error {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("Discover characteristics failed: \(error.localizedDescription)")
            exit(1)
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
        guard characteristic.uuid == eventUUID else { return }
        if let error {
            eventNotifyEnabled = false
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("Event subscription failed: \(error.localizedDescription)")
            return
        }
        eventNotifyEnabled = characteristic.isNotifying
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
        guard characteristic.uuid == eventUUID,
              let data = characteristic.value,
              let text = String(data: data, encoding: .utf8) else {
            return
        }
        log("event \(text)")
        handleDeviceEvent(text)
    }

    private func handleDeviceEvent(_ text: String) {
        switch text {
        case "input_primary_down":
            suppressLegacyPrimaryEventsUntil = Date().addingTimeInterval(0.25)
            handlePrimaryInputDown()
        case "input_primary_up":
            suppressLegacyPrimaryEventsUntil = Date().addingTimeInterval(0.25)
            handlePrimaryInputUp()
        case "input_primary_tap":
            suppressLegacyPrimaryEventsUntil = Date().addingTimeInterval(0.25)
            handlePrimaryInputTap()
        case "input_confirm_tap":
            suppressLegacyConfirmEventsUntil = Date().addingTimeInterval(0.25)
            handleCodexEnterRequest()
        case "typeless_option_tap_host":
            guard Date() >= suppressLegacyPrimaryEventsUntil else {
                log("legacy typeless tap ignored after generic input event")
                return
            }
            handlePrimaryInputTap()
        case "typeless_option_down":
            guard Date() >= suppressLegacyPrimaryEventsUntil else {
                log("legacy typeless down ignored after generic input event")
                return
            }
            handlePrimaryInputDown()
        case "typeless_option_up":
            guard Date() >= suppressLegacyPrimaryEventsUntil else {
                log("legacy typeless up ignored after generic input event")
                return
            }
            handlePrimaryInputUp()
        case "codex_enter":
            guard Date() >= suppressLegacyConfirmEventsUntil else {
                log("legacy codex enter ignored after generic input event")
                return
            }
            handleCodexEnterRequest()
        case "shake_action":
            handleShakeActionRequest()
        case "typeless_option_tap":
            log("legacy typeless event ignored; firmware owns HID key")
        default:
            return
        }
    }

    private func scanForDevice(hidServiceOnly: Bool) {
        guard central.state == .poweredOn else { return }
        let services = hidServiceOnly ? [hidServiceUUID] : nil
        central.scanForPeripherals(withServices: services,
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    private func resetBridgeCharacteristics() {
        eventCharacteristic = nil
        statusCharacteristic = nil
        panelCharacteristic = nil
        eventNotifyEnabled = false
        lastRediscoverAt = Date.distantPast
        lastBridgeConfigPayload = nil
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
            scanForDevice(hidServiceOnly: true)
            return
        }

        let missingBridge = eventCharacteristic == nil ||
            statusCharacteristic == nil ||
            !eventNotifyEnabled

        if missingBridge && Date().timeIntervalSince(lastRediscoverAt) > 8 {
            lastRediscoverAt = Date()
            log("Bridge health check: rediscovering services/characteristics.")
            peripheral.discoverServices([serviceUUID])
        }

        if eventCharacteristic != nil && !eventNotifyEnabled {
            peripheral.setNotifyValue(true, for: eventCharacteristic!)
        }
        if statusCharacteristic != nil {
            sendBridgeHeartbeat()
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
    }

    private func sendBridgeHeartbeat(force: Bool = false) {
        guard let peripheral,
              let characteristic = statusCharacteristic,
              let payload = Optional(bridgeReadyPayload()),
              let data = payload.data(using: .utf8) else {
            return
        }
        guard force || payload != lastBridgeConfigPayload else {
            return
        }
        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.write) ? .withResponse : .withoutResponse
        peripheral.writeValue(data, for: characteristic, type: writeType)
        lastBridgeConfigPayload = payload
        log("bridge config synced: \(payload)")
    }

    private func bridgeReadyPayload() -> String {
        let settings = SettingsStore.shared.settings
        let mode = settings.inputMode == .wechatIME ? "wechat_ime" : "typeless"
        let primary = settings.inputMode == .wechatIME ? hidReportBinding(settings.leftKey) : (modifier: UInt8(0), keycode: UInt8(0))
        let confirm = hidReportBinding(settings.rightKey)
        let shake = settings.shakeKey.map(hidReportBinding) ?? (modifier: UInt8(0), keycode: UInt8(0))
        let payload: [String: Any] = [
            "type": "bridge_ready",
            "version": 1,
            "input_mode": mode,
            "primary_modifier": Int(primary.modifier),
            "primary_key": Int(primary.keycode),
            "confirm_modifier": Int(confirm.modifier),
            "confirm_key": Int(confirm.keycode),
            "shake_action": settings.shakeActionName,
            "shake_modifier": Int(shake.modifier),
            "shake_key": Int(shake.keycode)
        ]
        if let data = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]),
           let text = String(data: data, encoding: .utf8) {
            return text
        }
        return "{\"type\":\"bridge_ready\",\"version\":1,\"input_mode\":\"\(mode)\"}"
    }

    private func sendBridgeLimited() {
        guard let peripheral,
              let characteristic = statusCharacteristic,
              let data = "{\"type\":\"bridge_limited\",\"version\":1,\"helper\":\"limited\"}".data(using: .utf8) else {
            return
        }
        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.write) ? .withResponse : .withoutResponse
        peripheral.writeValue(data, for: characteristic, type: writeType)
    }

    private func handlePrimaryInputDown() {
        switch SettingsStore.shared.settings.inputMode {
        case .typeless:
            handleTypelessToggleRequest()
        case .wechatIME:
            handleWeChatOptionDown()
        }
    }

    private func handlePrimaryInputUp() {
        switch SettingsStore.shared.settings.inputMode {
        case .typeless:
            log("typeless primary up ignored; Typeless mode uses toggle")
        case .wechatIME:
            handleWeChatOptionUp()
        }
    }

    private func handlePrimaryInputTap() {
        switch SettingsStore.shared.settings.inputMode {
        case .typeless:
            handleTypelessToggleRequest()
        case .wechatIME:
            handleWeChatOptionTap()
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

    private func handleWeChatOptionTap() {
        handleWeChatOptionDown()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) { [weak self] in
            self?.handleWeChatOptionUp()
        }
    }

    private func handleCodexEnterRequest() {
        guard AXIsProcessTrusted() else {
            sendBridgeLimited()
            log("codex enter ignored: Accessibility unavailable.")
            return
        }

        if SettingsStore.shared.settings.inputMode == .wechatIME {
            if wechatOptionDown {
                handleWeChatOptionUp()
            }
            let binding = SettingsStore.shared.settings.rightKey
            postKeyTap(binding)
            log("wechat input confirm via mac helper key=\(binding.name)")
            return
        }

        if processingUntil != nil || lastState?.phase == "processing" {
            processingUntil = nil
            pendingCodexEnter = false
            log("codex enter requested while Typeless processing; sending immediately.")
            write(VoiceState(active: false, phase: "idle", message: ""))
            performCodexEnter(reason: "direct-processing")
            return
        }
        performCodexEnter(reason: "direct")
    }

    private func performCodexEnter(reason: String) {
        let restored: Bool
        let targetDescription: String

        if let target = lockedInputTarget {
            restored = restoreInputFocus(target)
            targetDescription = "\(target.focus.appName) role=\(target.role)"
        } else if let snapshot = typelessFocusSnapshot {
            restored = restoreFocus(snapshot)
            targetDescription = "\(snapshot.appName) window=\(snapshot.windowTitle)"
        } else {
            log("codex enter ignored: no saved focus target.")
            pendingCodexEnter = false
            return
        }

        guard restored else {
            log("codex enter ignored: saved focus target is unavailable.")
            pendingCodexEnter = false
            return
        }

        let binding = SettingsStore.shared.settings.rightKey
        postKeyTap(binding)
        pendingCodexEnter = false
        log("codex enter via mac helper key=\(binding.name) reason=\(reason) target=\(targetDescription)")
    }

    private func handleShakeActionRequest() {
        guard AXIsProcessTrusted() else {
            sendBridgeLimited()
            log("shake action ignored: Accessibility unavailable.")
            return
        }

        switch SettingsStore.shared.settings.shakeActionName {
        case "None":
            log("shake action ignored: disabled.")
        case "Escape":
            postKeyTap(keyCode: UInt16(kVK_Escape))
            log("shake action sent Escape.")
        case "Return":
            handleCodexEnterRequest()
        case "Clear Input":
            performClearInput()
        default:
            if let binding = SettingsStore.shared.settings.shakeKey {
                postKeyTap(keyCode: binding.macKeyCode)
                log("shake action sent key \(binding.name).")
            } else {
                performClearInput()
            }
        }
    }

    private func performClearInput() {
        let restored: Bool
        let targetDescription: String

        if let target = lockedInputTarget {
            restored = restoreInputFocus(target)
            targetDescription = "\(target.focus.appName) role=\(target.role)"
        } else if let current = currentInputFocusTarget() {
            restored = restoreInputFocus(current)
            targetDescription = "\(current.focus.appName) role=\(current.role)"
        } else if let snapshot = typelessFocusSnapshot {
            restored = restoreFocus(snapshot)
            targetDescription = "\(snapshot.appName) window=\(snapshot.windowTitle)"
        } else {
            log("shake clear ignored: no input target.")
            return
        }

        guard restored else {
            log("shake clear ignored: input target unavailable.")
            return
        }

        postKeyTap(keyCode: UInt16(kVK_ANSI_A), flags: .maskCommand)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) {
            postKeyTap(keyCode: UInt16(kVK_Delete))
        }
        processingUntil = nil
        pendingCodexEnter = false
        write(VoiceState(active: false, phase: "idle", message: ""))
        log("shake clear input via mac helper target=\(targetDescription)")
    }

    private func handleTypelessToggleRequest() {
        guard AXIsProcessTrusted() else {
            typelessSessionActive = false
            typelessFocusSnapshot = nil
            lockedInputTarget = nil
            processingUntil = nil
            pendingCodexEnter = false
            lastState = nil
            sendBridgeLimited()
            log("typeless event ignored: Accessibility unavailable.")
            return
        }

        let shouldStop = typelessSessionActive ||
            lastState?.phase == "recording" ||
            lastState?.phase == "processing" ||
            processingUntil != nil

        if !shouldStop && !isTypelessRunning() {
            typelessSessionActive = false
            typelessFocusSnapshot = nil
            lockedInputTarget = nil
            processingUntil = nil
            pendingCodexEnter = false
            write(VoiceState(active: false, phase: "idle", message: "Typeless 未运行"))
            log("typeless start ignored: Typeless is not running; skipped \(SettingsStore.shared.settings.leftKey.name).")
            return
        }

        if !shouldStop {
            let target = currentInputFocusTarget()
            let focus = target?.focus ?? frontmostFocusSnapshot()
            guard let focus else {
                log("typeless start ignored: no frontmost focus snapshot.")
                return
            }
            lockedInputTarget = target
            typelessFocusSnapshot = focus
            let binding = SettingsStore.shared.settings.leftKey
            postKeyTap(binding)
            typelessSessionActive = true
            write(VoiceState(active: true, phase: "recording", message: "正在录制中"))
            if let target {
                log("typeless start via mac helper key=\(binding.name) target=\(target.focus.appName) role=\(target.role)")
            } else {
                log("typeless start via mac helper key=\(binding.name) target=\(focus.appName) window=\(focus.windowTitle)")
            }
            return
        }

        let target = lockedInputTarget
        let snapshot = typelessFocusSnapshot
        let binding = SettingsStore.shared.settings.leftKey
        postKeyTap(binding)
        restoreInputFocusAfterTypelessShortcut(target: target, snapshot: snapshot, reason: "stop")
        typelessSessionActive = false
        processingUntil = Date().addingTimeInterval(3.0)

        write(VoiceState(active: true, phase: "processing", message: ""))
        scheduleProcessingFollowUps()
        log("typeless stop via mac helper key=\(binding.name); focus restore scheduled")
    }

    private func scheduleProcessingFollowUps() {
        for delay in [0.35, 0.8, 1.4, 3.1] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                self?.writeCurrentState(force: false)
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
        pushQuotaPanel()
        quotaTimer?.invalidate()
        quotaTimer = Timer.scheduledTimer(withTimeInterval: TimeInterval(SettingsStore.shared.settings.quotaRefreshSeconds), repeats: true) { [weak self] _ in
            self?.pushQuotaPanel()
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

    private func pushQuotaPanel() {
        guard panelCharacteristic != nil else {
            return
        }
        guard !quotaFetchInFlight else {
            return
        }

        quotaFetchInFlight = true
        BridgeStatusCenter.shared.quotaStatus = "Fetching"
        DispatchQueue.global(qos: .utility).async { [weak self] in
            do {
                let panel = try buildDevicePanel()
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.quotaFetchInFlight = false
                    self.sendPanelData(panel,
                                       kind: "quota",
                                       finalStatus: "Synced \(DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium))",
                                       reason: "quota")
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
            sendPanelChunks(chunks,
                            seq: seq,
                            bytes: bytes.count,
                            kind: kind,
                            finalStatus: finalStatus,
                            reason: reason,
                            characteristic: characteristic)
        } catch {
            BridgeStatusCenter.shared.lastError = error.localizedDescription
            log("\(kind) panel encode failed: \(error.localizedDescription)")
        }
    }

    private func sendPanelChunks(_ chunks: [Data],
                                 seq: Int,
                                 bytes: Int,
                                 kind: String,
                                 finalStatus: String,
                                 reason: String,
                                 characteristic: CBCharacteristic,
                                 index: Int = 0) {
        guard let peripheral, index < chunks.count else {
            if kind == "quota" || kind == "time" {
                BridgeStatusCenter.shared.quotaStatus = finalStatus
            }
            log("\(kind) panel pushed seq=\(seq) bytes=\(bytes) parts=\(chunks.count) reason=\(reason)")
            return
        }

        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.write) ? .withResponse : .withoutResponse
        peripheral.writeValue(chunks[index], for: characteristic, type: writeType)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.03) { [weak self] in
            self?.sendPanelChunks(chunks,
                                  seq: seq,
                                  bytes: bytes,
                                  kind: kind,
                                  finalStatus: finalStatus,
                                  reason: reason,
                                  characteristic: characteristic,
                                  index: index + 1)
        }
    }

    private func writeCurrentState(force: Bool) {
        let state = resolvedState()
        if state.phase == "idle" && state.message != "Typeless 待机" {
            typelessSessionActive = false
        }
        if pendingCodexEnter && state.phase == "idle" && processingUntil == nil {
            pendingCodexEnter = false
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.65) { [weak self] in
                self?.performCodexEnter(reason: "queued")
            }
        }
        guard force || state != lastState else {
            return
        }
        write(state)
        lastState = state
    }

    private func resolvedState() -> VoiceState {
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
            if Date() >= until {
                processingUntil = nil
                if detected.phase == "recording" {
                    return detected
                }
                return VoiceState(active: false, phase: "idle", message: "")
            }
            if detected.phase == "idle" && detected.message != "Typeless 待机" {
                processingUntil = nil
                return detected
            }
            if detected.phase == "recording" {
                processingUntil = nil
                return detected
            }
            if detected.phase == "processing" {
                return detected
            }
            return VoiceState(active: true, phase: "processing", message: "")
        }
        let detected = currentTypelessState()
        if typelessSessionActive && detected.phase == "idle" && detected.message == "Typeless 待机" {
            return VoiceState(active: true, phase: "recording", message: "正在录制中")
        }
        return detected
    }

    private func write(_ state: VoiceState) {
        guard let peripheral,
              let characteristic = statusCharacteristic,
              let data = state.payload.data(using: .utf8) else {
            return
        }
        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.write) ? .withResponse : .withoutResponse
        peripheral.writeValue(data, for: characteristic, type: writeType)
        BridgeStatusCenter.shared.typelessStatus = state.message.isEmpty ? state.phase : state.message
        log("state \(state.phase) active=\(state.active) message=\(state.message)")
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
