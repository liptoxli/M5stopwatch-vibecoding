import Foundation
import CoreAudio
import AudioToolbox

private func require(_ condition: @autoclosure () -> Bool, _ message: String) {
    guard condition() else { fatalError("FAIL: \(message)") }
    print("PASS: \(message)")
}

private func propertyIDs(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector,
                         _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> [AudioObjectID] {
    var address = AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(object, &address, 0, nil, &size) == noErr, size > 0 else { return [] }
    var values = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    let status = values.withUnsafeMutableBytes { AudioObjectGetPropertyData(object, &address, 0, nil, &size, $0.baseAddress!) }
    return status == noErr ? values : []
}

private func processOutputDevices(pid targetPID: pid_t = getpid()) -> [AudioDeviceID] {
    guard #available(macOS 14.2, *) else { fatalError("Hardware route test needs macOS 14.2+") }
    for process in propertyIDs(AudioObjectID(kAudioObjectSystemObject), kAudioHardwarePropertyProcessObjectList) {
        var address = AudioObjectPropertyAddress(mSelector: kAudioProcessPropertyPID, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
        var pid: pid_t = 0, size = UInt32(MemoryLayout<pid_t>.size)
        if AudioObjectGetPropertyData(process, &address, 0, nil, &size, &pid) == noErr && pid == targetPID {
            return propertyIDs(process, kAudioProcessPropertyDevices, kAudioObjectPropertyScopeOutput)
        }
    }
    return []
}

private func checked(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    guard condition() else { throw NSError(domain: "AudioRouteTests", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
    print("PASS: \(message)")
}

private func setVirtualRate(_ device: AudioDeviceID, _ rate: Double) throws {
    try checked(MicrophoneCoreAudio.uid(of: device) == "M5StopWatchMic_2_UID", "rate test is restricted to M5 virtual output")
    var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyNominalSampleRate, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var value = rate
    let status = AudioObjectSetPropertyData(device, &address, 0, nil, UInt32(MemoryLayout<Double>.size), &value)
    guard status == noErr else { throw NSError(domain: "AudioRateTest", code: Int(status)) }
    for _ in 0..<20 {
        if MicrophoneCoreAudio.sampleRate(of: device) == rate { return }
        RunLoop.current.run(until: Date().addingTimeInterval(0.05))
    }
    throw NSError(domain: "AudioRateTest", code: 1, userInfo: [NSLocalizedDescriptionKey: "Virtual rate did not settle"])
}

/// Reads only the M5 virtual input; retains RMS/counts, never audio recordings.
private final class VirtualInputMeter {
    private let lock = NSLock()
    private var sum: Double = 0
    private var count = 0
    let device: AudioDeviceID
    private var ioProc: AudioDeviceIOProcID?
    init(device: AudioDeviceID) throws {
        self.device = device
        guard MicrophoneCoreAudio.uid(of: device) == "M5StopWatchMic_UID" else { fatalError("Refusing non-M5 capture") }
        let status = AudioDeviceCreateIOProcID(device, { _, _, input, _, _, _, ref in
            guard let ref else { return noErr }
            let meter = Unmanaged<VirtualInputMeter>.fromOpaque(ref).takeUnretainedValue()
            meter.lock.lock(); defer { meter.lock.unlock() }
            for buffer in UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: input)) {
                guard let data = buffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
                for i in 0..<(Int(buffer.mDataByteSize) / 4) {
                    meter.sum += Double(data[i]) * Double(data[i]); meter.count += 1
                }
            }
            return noErr
        }, Unmanaged.passUnretained(self).toOpaque(), &ioProc)
        guard status == noErr, let ioProc else { throw NSError(domain: "VirtualInputTest", code: Int(status)) }
        let start = AudioDeviceStart(device, ioProc)
        guard start == noErr else { stop(); throw NSError(domain: "VirtualInputTest", code: Int(start)) }
    }
    func reset() { lock.lock(); defer { lock.unlock() }; sum = 0; count = 0 }
    func rms() -> Double { lock.lock(); defer { lock.unlock() }; return count > 0 ? sqrt(sum / Double(count)) : -1 }
    func stop() { if let ioProc { AudioDeviceStop(device, ioProc); AudioDeviceDestroyIOProcID(device, ioProc) }; ioProc = nil }
    deinit { stop() }
}

@main
enum AudioRouteTests {
    static func main() {
        do { try run() }
        catch { print("FAIL: \(error.localizedDescription)"); exit(1) }
    }

    static func run() throws {
        if let index = CommandLine.arguments.firstIndex(of: "--audit-pid") {
            guard CommandLine.arguments.indices.contains(index + 1),
                  let pid = pid_t(CommandLine.arguments[index + 1]), pid > 0 else {
                throw NSError(domain: "AudioRouteAudit", code: 1, userInfo: [NSLocalizedDescriptionKey: "--audit-pid requires a positive process ID"])
            }
            let outputs = processOutputDevices(pid: pid)
            for id in outputs {
                print("PID \(pid) output=\(id) UID=\(MicrophoneCoreAudio.uid(of: id) ?? "unknown") rate=\(MicrophoneCoreAudio.sampleRate(of: id) ?? 0)")
            }
            for id in propertyIDs(AudioObjectID(kAudioObjectSystemObject), kAudioHardwarePropertyDefaultOutputDevice) {
                print("System default output=\(id) UID=\(MicrophoneCoreAudio.uid(of: id) ?? "unknown")")
            }
            if let id = MicrophoneCoreAudio.defaultInput() {
                print("System default input=\(id) UID=\(MicrophoneCoreAudio.uid(of: id) ?? "unknown")")
            }
            guard !outputs.isEmpty else {
                print("INACTIVE: no output device for this PID; connect the StopWatch and enable its virtual microphone before auditing")
                exit(2)
            }
            try checked(outputs.allSatisfy(MicrophoneCoreAudio.isAllowedOutput), "running process outputs exclusively to allowed virtual devices")
            return
        }
        let gate = MicrophoneRouteGate(device: 77)
        require(!gate.permits(77), "output starts muted")
        gate.arm()
        require(gate.permits(77), "verified virtual route permits audio")
        require(!gate.permits(102), "speaker route is rejected")
        gate.arm()
        require(!gate.permits(77), "route fault latches until output reconstruction")
        let failedRead = MicrophoneRouteGate(device: 77)
        failedRead.arm()
        require(!failedRead.permits(nil), "failed CurrentDevice read mutes output")
        let unknown = MicrophoneRouteGate(device: kAudioObjectUnknown)
        unknown.arm()
        require(!unknown.permits(kAudioObjectUnknown), "unknown device never permits audio")
        let muted = MicrophoneRouteGate(device: 77)
        muted.arm(); muted.mute()
        require(!muted.permits(77), "stopped output is muted")

        guard CommandLine.arguments.contains("--hardware") else { return }
        guard let target = MicrophoneCoreAudio.virtualDevice() else { fatalError("Install M5 StopWatch Mic before hardware tests") }
        let defaultInputBefore = MicrophoneCoreAudio.defaultInput()
        let defaultOutputBefore = propertyIDs(AudioObjectID(kAudioObjectSystemObject), kAudioHardwarePropertyDefaultOutputDevice)
        let rateBefore = MicrophoneCoreAudio.sampleRate(of: target.output)
        let ring = MicrophoneAudioRing()
        for bad in defaultOutputBefore where !MicrophoneCoreAudio.isAllowedOutput(bad) {
            do {
                let rejected = try VirtualMicrophoneOutput(ring: ring, device: bad)
                rejected.stop()
                fatalError("Accepted physical speaker as microphone output")
            } catch { print("PASS: physical output rejected before audio unit creation") }
        }
        for _ in 0..<3 {
            let output = try VirtualMicrophoneOutput(ring: ring, device: target.output)
            RunLoop.current.run(until: Date().addingTimeInterval(0.3))
            let health = output.health(now: Date())
            require(health.healthy && health.renderCount > 0, "pinned output renders silence and verifies its route")
            let actual = processOutputDevices()
            require(!actual.isEmpty && actual.allSatisfy { $0 == target.output }, "Core Audio process output is ONLY the virtual device: \(actual)")
            output.invalidateRouteForTesting()
            require(!output.health(now: Date()).healthy, "invalidated route reports unhealthy")
            output.stop(); output.stop()
            require(!output.health(now: Date()).engineRunning, "idempotent teardown stops IO")
        }
        require(MicrophoneCoreAudio.defaultInput() == defaultInputBefore, "output tests preserve system input")
        require(propertyIDs(AudioObjectID(kAudioObjectSystemObject), kAudioHardwarePropertyDefaultOutputDevice) == defaultOutputBefore, "output tests preserve system speakers")
        require(MicrophoneCoreAudio.sampleRate(of: target.output) == rateBefore, "output tests preserve device sample rate")

        if CommandLine.arguments.contains("--loopback") {
            ring.reset()
            let output = try VirtualMicrophoneOutput(ring: ring, device: target.output)
            defer { output.stop() }
            let meter = try VirtualInputMeter(device: target.input)
            defer { meter.stop() }
            RunLoop.current.run(until: Date().addingTimeInterval(0.2))
            let actual = processOutputDevices()
            require(!actual.isEmpty && actual.allSatisfy { $0 == target.output }, "synthetic loopback cannot target speakers")
            meter.reset()
            ring.append((0..<16_000).map { Int16(sin(Double($0) * 2 * .pi * 440 / 16_000) * 3000) })
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
            let rms = meter.rms()
            require(rms > 0.01 && rms < 0.15, "16 kHz PCM reaches virtual INPUT with expected level: rms=\(rms)")
            output.invalidateRouteForTesting()
            RunLoop.current.run(until: Date().addingTimeInterval(0.15))
            meter.reset()
            RunLoop.current.run(until: Date().addingTimeInterval(0.25))
            require(meter.rms() >= 0 && meter.rms() < 0.00001, "route fault silences FINAL output, including converter tail: rms=\(meter.rms())")
        }

        if CommandLine.arguments.contains("--rates"), let rateBefore {
            // Throwing checks ensure the original virtual rate is restored even
            // on a failed assertion. No physical device property is changed.
            defer { try? setVirtualRate(target.output, rateBefore) }
            let oldOutput = try VirtualMicrophoneOutput(ring: ring, device: target.output)
            defer { oldOutput.stop() }
            try setVirtualRate(target.output, rateBefore == 44100 ? 48000 : 44100)
            RunLoop.current.run(until: Date().addingTimeInterval(0.2))
            try checked(!oldOutput.health(now: Date()).routeValid, "real device sample-rate change latches the route fault")
            oldOutput.stop()
            for rate in [16000.0, 44100.0, 48000.0] {
                try setVirtualRate(target.output, rate)
                ring.reset()
                let output = try VirtualMicrophoneOutput(ring: ring, device: target.output)
                defer { output.stop() }
                let meter = try VirtualInputMeter(device: target.input)
                defer { meter.stop() }
                // The driver advertises a 16384-frame zero-timestamp period;
                // allow one period after changing the shared device clock.
                RunLoop.current.run(until: Date().addingTimeInterval(16384.0 / rate + 0.25))
                try checked(output.health(now: Date()).healthy, "output reconstructs at \(rate) Hz")
                let actual = processOutputDevices()
                try checked(!actual.isEmpty && actual.allSatisfy { $0 == target.output }, "\(rate) Hz process route remains virtual-only")
                meter.reset()
                ring.append((0..<16000).map { Int16(sin(Double($0) * 2 * .pi * 440 / 16000) * 3000) })
                RunLoop.current.run(until: Date().addingTimeInterval(0.6))
                let rms = meter.rms()
                try checked(rms > 0.01 && rms < 0.15, "\(rate) Hz input receives converted PCM: rms=\(rms)")
                meter.stop(); output.stop()
            }
        }
    }
}
