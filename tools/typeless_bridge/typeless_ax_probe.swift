import AppKit
import ApplicationServices
import Carbon
import Foundation

func stringValue(_ value: CFTypeRef?) -> String {
    guard let value else { return "" }
    if CFGetTypeID(value) == CFStringGetTypeID() {
        return value as! String
    }
    if CFGetTypeID(value) == CFNumberGetTypeID() {
        return "\(value)"
    }
    return ""
}

func copyAttribute(_ element: AXUIElement, _ attr: CFString) -> CFTypeRef? {
    var value: CFTypeRef?
    let err = AXUIElementCopyAttributeValue(element, attr, &value)
    if err != .success {
        return nil
    }
    return value
}

func describe(_ element: AXUIElement, prefix: String = "") {
    let role = stringValue(copyAttribute(element, kAXRoleAttribute as CFString))
    let title = stringValue(copyAttribute(element, kAXTitleAttribute as CFString))
    let value = stringValue(copyAttribute(element, kAXValueAttribute as CFString))
    let description = stringValue(copyAttribute(element, kAXDescriptionAttribute as CFString))
    let help = stringValue(copyAttribute(element, kAXHelpAttribute as CFString))
    if !role.isEmpty || !title.isEmpty || !value.isEmpty || !description.isEmpty || !help.isEmpty {
        print("\(prefix)role=\(role) title=\(title) value=\(value) desc=\(description) help=\(help)")
    }
}

func children(_ element: AXUIElement, limit: Int = 30) -> [AXUIElement] {
    guard let raw = copyAttribute(element, kAXChildrenAttribute as CFString) else {
        return []
    }
    guard CFGetTypeID(raw) == CFArrayGetTypeID() else {
        return []
    }
    let array = raw as! [AXUIElement]
    return Array(array.prefix(limit))
}

let trusted = AXIsProcessTrusted()
print("AXTrusted=\(trusted)")

let holdRightOptionSeconds: TimeInterval = {
    guard let index = CommandLine.arguments.firstIndex(of: "--hold-right-option"),
          CommandLine.arguments.count > index + 1,
          let seconds = Double(CommandLine.arguments[index + 1]) else {
        return 0
    }
    return seconds
}()

let tapRightOptionSpace = CommandLine.arguments.contains("--tap-right-option-space")

func postRightOption(isDown: Bool) {
    let event = CGEvent(
        keyboardEventSource: nil,
        virtualKey: CGKeyCode(kVK_RightOption),
        keyDown: isDown
    )
    event?.flags = isDown ? .maskAlternate : []
    event?.post(tap: .cghidEventTap)
}

func tapSpaceWithRightOption() {
    let source = CGEventSource(stateID: .hidSystemState)
    let optionDown = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(kVK_RightOption), keyDown: true)
    optionDown?.flags = .maskAlternate
    optionDown?.post(tap: .cghidEventTap)

    let spaceDown = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(kVK_Space), keyDown: true)
    spaceDown?.flags = .maskAlternate
    spaceDown?.post(tap: .cghidEventTap)

    let spaceUp = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(kVK_Space), keyDown: false)
    spaceUp?.flags = .maskAlternate
    spaceUp?.post(tap: .cghidEventTap)

    let optionUp = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(kVK_RightOption), keyDown: false)
    optionUp?.flags = []
    optionUp?.post(tap: .cghidEventTap)
}

let apps = NSWorkspace.shared.runningApplications.filter {
    $0.bundleIdentifier == "now.typeless.desktop" || $0.localizedName == "Typeless"
}

guard let app = apps.first else {
    print("TypelessNotRunning")
    exit(2)
}

print("Typeless pid=\(app.processIdentifier) active=\(app.isActive) hidden=\(app.isHidden) terminated=\(app.isTerminated)")

let axApp = AXUIElementCreateApplication(app.processIdentifier)
describe(axApp, prefix: "app ")

if holdRightOptionSeconds > 0 {
    print("Holding RightOption for \(holdRightOptionSeconds)s")
    postRightOption(isDown: true)
    Thread.sleep(forTimeInterval: min(holdRightOptionSeconds, 1.0))
}

if tapRightOptionSpace {
    print("Tapping RightOption+Space")
    tapSpaceWithRightOption()
    Thread.sleep(forTimeInterval: 1.0)
}

if let rawWindows = copyAttribute(axApp, kAXWindowsAttribute as CFString),
   CFGetTypeID(rawWindows) == CFArrayGetTypeID() {
    let windows = rawWindows as! [AXUIElement]
    print("windows=\(windows.count)")
    for (index, window) in windows.enumerated() {
        describe(window, prefix: "window[\(index)] ")
        for (childIndex, child) in children(window).enumerated() {
            describe(child, prefix: "  child[\(childIndex)] ")
            for (grandIndex, grand) in children(child, limit: 12).enumerated() {
                describe(grand, prefix: "    grand[\(grandIndex)] ")
            }
        }
    }
} else {
    print("windows=0")
}

for (index, child) in children(axApp).enumerated() {
    describe(child, prefix: "rootChild[\(index)] ")
}

if holdRightOptionSeconds > 0 {
    let remaining = max(holdRightOptionSeconds - 1.0, 0)
    if remaining > 0 {
        Thread.sleep(forTimeInterval: remaining)
    }
    postRightOption(isDown: false)
    print("Released RightOption")
}
