import AudioToolbox
import CoreAudio
import CoreBluetooth
import Foundation

let stopWatchMicrophoneServiceUUID = CBUUID(string: "7D2F0001-5CF1-4F3C-9F42-A8C8F6A1B001")
let stopWatchMicrophoneControlUUID = CBUUID(string: "7D2F0002-5CF1-4F3C-9F42-A8C8F6A1B001")
let stopWatchMicrophoneAudioUUID = CBUUID(string: "7D2F0003-5CF1-4F3C-9F42-A8C8F6A1B001")
let stopWatchMicrophoneStatsUUID = CBUUID(string: "7D2F0004-5CF1-4F3C-9F42-A8C8F6A1B001")
let stopWatchVirtualMicrophoneName = "M5 StopWatch Mic"

private let microphoneSampleRate = 16_000.0
private let microphoneOutputStallSeconds: TimeInterval = 2.5
private let imaIndexTable = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
private let imaStepTable = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767,
]

final class MicrophoneAudioRing {
    private var samples = Array(repeating: Float(0), count: Int(microphoneSampleRate * 3))
    private var readIndex = 0
    private var writeIndex = 0
    private var count = 0
    private let lock = NSLock()

    func reset() {
        lock.lock(); defer { lock.unlock() }
        readIndex = 0; writeIndex = 0; count = 0
    }

    func append(_ pcm: [Int16]) {
        lock.lock(); defer { lock.unlock() }
        for value in pcm {
            if count == samples.count {
                readIndex = (readIndex + 1) % samples.count
                count -= 1
            }
            samples[writeIndex] = Float(value) / 32_768.0
            writeIndex = (writeIndex + 1) % samples.count
            count += 1
        }
    }

    func appendSilence(_ count: Int) {
        guard count > 0 else { return }
        append(Array(repeating: 0, count: count))
    }

    func render(into target: UnsafeMutablePointer<Float>, frames: Int) {
        lock.lock(); defer { lock.unlock() }
        for index in 0..<frames {
            guard count > 0 else { target[index] = 0; continue }
            target[index] = samples[readIndex]
            readIndex = (readIndex + 1) % samples.count
            count -= 1
        }
    }
}

struct MicrophonePipelineHealth {
    let outputConfigured: Bool
    let engineRunning: Bool
    let outputHealthy: Bool
    let outputRouteValid: Bool
    let outputRouteDescription: String
    let outputAge: TimeInterval?
    let renderAge: TimeInterval?
    let renderCount: UInt64
    let packetAge: TimeInterval?
    let packetCount: UInt64
    let lostPacketCount: UInt64
    let latestPacketGapCount: UInt64
    let decodedRMS: Double
    let statsAge: TimeInterval?
    let deviceStreaming: Bool
    let deviceAudioSubscribed: Bool
    let devicePacketsSent: UInt32
    let devicePacketsDropped: UInt32
    let deviceNotifyError: Int32
    let deviceConsecutiveNotifyFailures: UInt32
    let deviceFreeMbufs: UInt32
}

private final class MicrophoneRenderHeartbeat {
    private let lock = NSLock()
    private var lastRenderAt: Date?
    private var renderCount: UInt64 = 0

    func markRendered() {
        lock.lock(); defer { lock.unlock() }
        lastRenderAt = Date()
        renderCount &+= 1
    }

    func snapshot(now: Date) -> (age: TimeInterval?, count: UInt64) {
        lock.lock(); defer { lock.unlock() }
        return (lastRenderAt.map { now.timeIntervalSince($0) }, renderCount)
    }
}

enum MicrophoneCoreAudio {
    static let allowedOutputUIDs: Set<String> = ["M5StopWatchMic_2_UID", "BlackHole2ch_UID"]

    static func uid(of device: AudioDeviceID) -> String? {
        var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyDeviceUID,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        guard AudioObjectGetPropertyData(device, &address, 0, nil, &size, &value) == noErr else { return nil }
        return value?.takeRetainedValue() as String?
    }

    static func isAllowedOutput(_ device: AudioDeviceID) -> Bool {
        guard let uid = uid(of: device), allowedOutputUIDs.contains(uid) else { return false }
        var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyTransportType,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var transport: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        return AudioObjectGetPropertyData(device, &address, 0, nil, &size, &transport) == noErr &&
            transport == kAudioDeviceTransportTypeVirtual
    }

    static func sampleRate(of device: AudioDeviceID) -> Double? {
        var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyNominalSampleRate,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var rate: Float64 = 0
        var size = UInt32(MemoryLayout<Float64>.size)
        return AudioObjectGetPropertyData(device, &address, 0, nil, &size, &rate) == noErr ? rate : nil
    }

    static func isAlive(_ device: AudioDeviceID) -> Bool {
        var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyDeviceIsAlive,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var alive: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        return AudioObjectGetPropertyData(device, &address, 0, nil, &size, &alive) == noErr && alive != 0
    }
    static func devices() -> [AudioDeviceID] {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size) == noErr else { return [] }
        var result = Array(repeating: AudioDeviceID(0), count: Int(size) / MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &result) == noErr else { return [] }
        return result
    }

    static func name(of device: AudioDeviceID) -> String? {
        var address = AudioObjectPropertyAddress(mSelector: kAudioObjectPropertyName,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        guard AudioObjectGetPropertyData(device, &address, 0, nil, &size, &value) == noErr else { return nil }
        return value?.takeRetainedValue() as String?
    }

    static func device(uid: String) -> AudioDeviceID? {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyTranslateUIDToDevice,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var uidValue: CFString = uid as CFString
        var device = AudioDeviceID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        let status = withUnsafePointer(to: &uidValue) { pointer in
            AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address,
                                       UInt32(MemoryLayout<CFString>.size), pointer,
                                       &size, &device)
        }
        return status == noErr && device != kAudioObjectUnknown ? device : nil
    }

    static func virtualDevice() -> (input: AudioDeviceID, output: AudioDeviceID, name: String)? {
        let allDevices = devices()
        if let input = device(uid: "M5StopWatchMic_UID"),
           let output = device(uid: "M5StopWatchMic_2_UID"), isAllowedOutput(output) {
            return (input, output, stopWatchVirtualMicrophoneName)
        }
        if let blackHole = allDevices.first(where: { uid(of: $0) == "BlackHole2ch_UID" }), isAllowedOutput(blackHole) {
            return (blackHole, blackHole, "BlackHole 2ch")
        }
        return nil
    }

    static func defaultInput() -> AudioDeviceID? {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultInputDevice,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var device = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        return AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &device) == noErr ? device : nil
    }

    static func setDefaultInput(_ device: AudioDeviceID) throws {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultInputDevice,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var value = device
        let status = AudioObjectSetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil,
                                                UInt32(MemoryLayout<AudioDeviceID>.size), &value)
        guard status == noErr else {
            throw NSError(domain: "M5StopWatchMic", code: Int(status),
                          userInfo: [NSLocalizedDescriptionKey: "Cannot select default input (Core Audio \(status))"])
        }
    }
}

/// Fail closed. A route fault stays latched until the output is rebuilt, even
/// if the device subsequently reappears. Never replay buffered speech on repair.
final class MicrophoneRouteGate {
    let expectedDevice: AudioDeviceID
    private let lock = NSLock()
    private var enabled = false
    private var faulted = false

    init(device: AudioDeviceID) { expectedDevice = device }
    func arm() { lock.lock(); defer { lock.unlock() }; enabled = !faulted }
    func mute() { lock.lock(); defer { lock.unlock() }; enabled = false }
    func invalidate() { lock.lock(); defer { lock.unlock() }; enabled = false; faulted = true }
    var isFaulted: Bool { lock.lock(); defer { lock.unlock() }; return faulted }
    func permits(_ actual: AudioDeviceID?) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard actual == expectedDevice, expectedDevice != kAudioObjectUnknown else {
            enabled = false; faulted = true
            return false
        }
        return enabled && !faulted
    }
}

private func microphoneOutputError(_ message: String, _ status: OSStatus = -1) -> NSError {
    NSError(domain: "M5StopWatchMic", code: Int(status), userInfo: [NSLocalizedDescriptionKey: message])
}

private func checkMicrophoneAudioStatus(_ status: OSStatus, _ operation: String) throws {
    guard status == noErr else { throw microphoneOutputError("\(operation) (Core Audio \(status))", status) }
}

private func currentMicrophoneOutputDevice(_ unit: AudioUnit?) -> AudioDeviceID? {
    guard let unit else { return nil }
    var device: AudioDeviceID = kAudioObjectUnknown
    var size = UInt32(MemoryLayout<AudioDeviceID>.size)
    let status = AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                     kAudioUnitScope_Global, 0, &device, &size)
    return status == noErr && device != kAudioObjectUnknown ? device : nil
}

private final class MicrophoneOutputContext {
    let ring: MicrophoneAudioRing
    let gate: MicrophoneRouteGate
    let heartbeat = MicrophoneRenderHeartbeat()
    var outputUnit: AudioUnit?
    var converterUnit: AudioUnit?
    var sampleRate: Double = 0
    let deviceUID: String
    init(ring: MicrophoneAudioRing, device: AudioDeviceID) {
        self.ring = ring
        gate = MicrophoneRouteGate(device: device)
        deviceUID = MicrophoneCoreAudio.uid(of: device) ?? ""
    }
    func routeMatches() -> Bool {
        currentMicrophoneOutputDevice(outputUnit) == gate.expectedDevice &&
            MicrophoneCoreAudio.uid(of: gate.expectedDevice) == deviceUID &&
            MicrophoneCoreAudio.isAlive(gate.expectedDevice) &&
            MicrophoneCoreAudio.sampleRate(of: gate.expectedDevice) == sampleRate
    }
}

private func silenceMicrophoneOutput(_ list: UnsafeMutablePointer<AudioBufferList>) {
    for buffer in UnsafeMutableAudioBufferListPointer(list) {
        if let data = buffer.mData { memset(data, 0, Int(buffer.mDataByteSize)) }
    }
}

private let microphoneSourceCallback: AURenderCallback = { ref, _, _, _, frames, data in
    guard let data else { return kAudio_ParamError }
    let context = Unmanaged<MicrophoneOutputContext>.fromOpaque(ref).takeUnretainedValue()
    let buffers = UnsafeMutableAudioBufferListPointer(data)
    guard let first = buffers.first, first.mNumberChannels == 1,
          first.mDataByteSize >= frames * 4, let pointer = first.mData?.assumingMemoryBound(to: Float.self) else {
        silenceMicrophoneOutput(data)
        context.gate.invalidate()
        return kAudio_ParamError
    }
    context.ring.render(into: pointer, frames: Int(frames))
    return noErr
}

/// Gate the FINAL output callback, not merely the source: a sample-rate
/// converter can retain previously supplied samples in its own buffer.
private let microphoneOutputCallback: AURenderCallback = { ref, flags, time, _, frames, data in
    guard let data else { return kAudio_ParamError }
    let context = Unmanaged<MicrophoneOutputContext>.fromOpaque(ref).takeUnretainedValue()
    guard context.gate.permits(currentMicrophoneOutputDevice(context.outputUnit)),
          let converter = context.converterUnit else {
        silenceMicrophoneOutput(data)
        flags.pointee.insert(.unitRenderAction_OutputIsSilence)
        return noErr
    }
    flags.pointee.remove(.unitRenderAction_OutputIsSilence)
    let status = AudioUnitRender(converter, flags, time, 0, frames, data)
    guard status == noErr, context.gate.permits(currentMicrophoneOutputDevice(context.outputUnit)) else {
        context.gate.invalidate()
        silenceMicrophoneOutput(data)
        flags.pointee.insert(.unitRenderAction_OutputIsSilence)
        return noErr
    }
    flags.pointee.remove(.unitRenderAction_OutputIsSilence)
    context.heartbeat.markRendered()
    return noErr
}

private let microphoneUnitRouteChanged: AudioUnitPropertyListenerProc = { ref, _, _, _, _ in
    let context = Unmanaged<MicrophoneOutputContext>.fromOpaque(ref).takeUnretainedValue()
    if !context.routeMatches() { context.gate.invalidate() }
}

private let microphoneDeviceChanged: AudioObjectPropertyListenerProc = { _, _, _, ref in
    guard let ref else { return noErr }
    let context = Unmanaged<MicrophoneOutputContext>.fromOpaque(ref).takeUnretainedValue()
    if !context.routeMatches() { context.gate.invalidate() }
    return noErr
}

/// Explicit AUHAL + AUConverter. Unlike an AVAudioEngine default output node,
/// this output owns one fixed virtual device; system speaker selection is not
/// part of this graph. AUConverter retains the original 16 kHz mono source and
/// converts to the device rate without changing the system's sample rate.
final class VirtualMicrophoneOutput {
    private let context: MicrophoneOutputContext
    private let device: AudioDeviceID
    private let deviceUID: String
    private var sampleRate: Double = 0
    private let startedAt = Date()
    private var unitListenerInstalled = false
    private var deviceListeners: [AudioObjectPropertyAddress] = []

    init(ring: MicrophoneAudioRing, device: AudioDeviceID) throws {
        guard MicrophoneCoreAudio.isAllowedOutput(device), MicrophoneCoreAudio.isAlive(device),
              let uid = MicrophoneCoreAudio.uid(of: device) else {
            throw microphoneOutputError("Refusing non-virtual or missing microphone output device \(device)")
        }
        self.device = device
        deviceUID = uid
        context = MicrophoneOutputContext(ring: ring, device: device)
        do { try configure() } catch { stop(); throw error }
    }

    private func makeUnit(type: OSType, subtype: OSType) throws -> AudioUnit {
        var description = AudioComponentDescription(componentType: type, componentSubType: subtype,
                                                     componentManufacturer: kAudioUnitManufacturer_Apple,
                                                     componentFlags: 0, componentFlagsMask: 0)
        guard let component = AudioComponentFindNext(nil, &description) else {
            throw microphoneOutputError("Cannot find microphone audio component")
        }
        var unit: AudioUnit?
        try checkMicrophoneAudioStatus(AudioComponentInstanceNew(component, &unit), "Create microphone audio unit")
        guard let unit else { throw microphoneOutputError("Cannot create microphone audio unit") }
        return unit
    }

    private func configure() throws {
        let output = try makeUnit(type: kAudioUnitType_Output, subtype: kAudioUnitSubType_HALOutput)
        context.outputUnit = output
        var enabled: UInt32 = 1, disabled: UInt32 = 0
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(output, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enabled, 4), "Enable virtual output")
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(output, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &disabled, 4), "Disable hardware input")
        var target = device
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(output, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &target, UInt32(MemoryLayout<AudioDeviceID>.size)), "Bind virtual microphone output")
        var hardwareFormat = AudioStreamBasicDescription()
        var formatSize = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
        try checkMicrophoneAudioStatus(AudioUnitGetProperty(output, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &hardwareFormat, &formatSize), "Read virtual device format")
        guard hardwareFormat.mSampleRate > 0, hardwareFormat.mSampleRate.isFinite,
              hardwareFormat.mChannelsPerFrame == 2 else {
            throw microphoneOutputError("Unsupported virtual microphone device format")
        }
        sampleRate = hardwareFormat.mSampleRate
        context.sampleRate = sampleRate
        var nativeFormat = AudioStreamBasicDescription(mSampleRate: sampleRate, mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
            mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4, mChannelsPerFrame: 2, mBitsPerChannel: 32, mReserved: 0)
        var sourceFormat = AudioStreamBasicDescription(mSampleRate: microphoneSampleRate, mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagsNativeFloatPacked,
            mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4, mChannelsPerFrame: 1, mBitsPerChannel: 32, mReserved: 0)
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(output, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &nativeFormat, formatSize), "Configure virtual output format")
        let converter = try makeUnit(type: kAudioUnitType_FormatConverter, subtype: kAudioUnitSubType_AUConverter)
        context.converterUnit = converter
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(converter, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &sourceFormat, formatSize), "Configure PCM input format")
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(converter, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &nativeFormat, formatSize), "Configure sample-rate conversion")
        var channels: [Int32] = [0, 0]
        try channels.withUnsafeMutableBytes { bytes in
            try checkMicrophoneAudioStatus(AudioUnitSetProperty(converter, kAudioOutputUnitProperty_ChannelMap, kAudioUnitScope_Output, 0, bytes.baseAddress!, UInt32(bytes.count)), "Map mono microphone to both virtual channels")
        }
        // A converter may request more source frames than the hardware slice.
        var maxFrames: UInt32 = 8192
        for unit in [output, converter] {
            try checkMicrophoneAudioStatus(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, 4), "Configure microphone render slice")
        }
        let ref = Unmanaged.passUnretained(context).toOpaque()
        var sourceCallback = AURenderCallbackStruct(inputProc: microphoneSourceCallback, inputProcRefCon: ref)
        var outputCallback = AURenderCallbackStruct(inputProc: microphoneOutputCallback, inputProcRefCon: ref)
        let callbackSize = UInt32(MemoryLayout<AURenderCallbackStruct>.size)
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(converter, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &sourceCallback, callbackSize), "Install PCM callback")
        try checkMicrophoneAudioStatus(AudioUnitSetProperty(output, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &outputCallback, callbackSize), "Install guarded virtual output callback")
        try checkMicrophoneAudioStatus(AudioUnitInitialize(converter), "Initialize microphone converter")
        try checkMicrophoneAudioStatus(AudioUnitInitialize(output), "Initialize pinned virtual output")
        try checkMicrophoneAudioStatus(AudioUnitAddPropertyListener(output, kAudioOutputUnitProperty_CurrentDevice, microphoneUnitRouteChanged, ref), "Watch microphone output route")
        unitListenerInstalled = true
        for selector in [kAudioDevicePropertyDeviceIsAlive, kAudioDevicePropertyNominalSampleRate] {
            var address = AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
            try checkMicrophoneAudioStatus(AudioObjectAddPropertyListener(device, &address, microphoneDeviceChanged, ref), "Watch virtual device changes")
            deviceListeners.append(address)
        }
        guard routeMatches() else { throw microphoneOutputError("Virtual output route verification failed before start") }
        // Start muted; no buffered audio can reach an unverified output.
        try checkMicrophoneAudioStatus(AudioOutputUnitStart(output), "Start pinned virtual output")
        guard routeMatches(), !context.gate.isFaulted else { throw microphoneOutputError("Virtual output route verification failed after start") }
        context.gate.arm()
    }

    private func routeMatches() -> Bool {
        context.routeMatches()
    }

    func stop() {
        context.gate.mute()
        let ref = Unmanaged.passUnretained(context).toOpaque()
        if let unit = context.outputUnit {
            AudioOutputUnitStop(unit)
            if unitListenerInstalled {
                AudioUnitRemovePropertyListenerWithUserData(unit, kAudioOutputUnitProperty_CurrentDevice, microphoneUnitRouteChanged, ref)
                unitListenerInstalled = false
            }
            for var address in deviceListeners {
                AudioObjectRemovePropertyListener(device, &address, microphoneDeviceChanged, ref)
            }
            deviceListeners.removeAll()
            AudioUnitUninitialize(unit)
            AudioComponentInstanceDispose(unit)
            context.outputUnit = nil
        }
        if let unit = context.converterUnit {
            AudioUnitUninitialize(unit)
            AudioComponentInstanceDispose(unit)
            context.converterUnit = nil
        }
    }

    deinit { stop() }

    func health(now: Date) -> (engineRunning: Bool, healthy: Bool, outputAge: TimeInterval,
                               renderAge: TimeInterval?, renderCount: UInt64, routeValid: Bool, routeDescription: String) {
        let actual = currentMicrophoneOutputDevice(context.outputUnit)
        let routeValid = routeMatches() && !context.gate.isFaulted
        if !routeValid { context.gate.invalidate() }
        var running: UInt32 = 0, size: UInt32 = 4
        if let unit = context.outputUnit {
            if AudioUnitGetProperty(unit, kAudioOutputUnitProperty_IsRunning, kAudioUnitScope_Global, 0, &running, &size) != noErr { running = 0 }
        }
        let render = context.heartbeat.snapshot(now: now)
        let outputAge = now.timeIntervalSince(startedAt)
        let responsive = render.age.map { $0 <= microphoneOutputStallSeconds } ?? (outputAge <= microphoneOutputStallSeconds)
        let description = "expected=\(deviceUID):\(device) actual=\(actual.map(String.init) ?? "unknown") rate=\(Int(sampleRate))"
        return (running != 0, running != 0 && responsive && routeValid, outputAge, render.age, render.count, routeValid, description)
    }

#if DEBUG
    func invalidateRouteForTesting() { context.gate.invalidate() }
#endif
}

final class StopWatchMicrophonePipeline {
    private let ring = MicrophoneAudioRing()
    private var output: VirtualMicrophoneOutput?
    private var previousInput: AudioDeviceID?
    private var expectedSequence: UInt16?
    private var expectedSampleIndex: UInt32?
    private var lastPacketAt: Date?
    private var lastStatsAt: Date?
    private var lastDecodedRMS: Double = 0
    private var deviceStreaming = false
    private var deviceAudioSubscribed = false
    private var devicePacketsSent: UInt32 = 0
    private var devicePacketsDropped: UInt32 = 0
    private var deviceNotifyError: Int32 = 0
    private var deviceConsecutiveNotifyFailures: UInt32 = 0
    private var deviceFreeMbufs: UInt32 = 0
    private(set) var outputDeviceName: String?
    private(set) var packetCount: UInt64 = 0
    private(set) var lostPacketCount: UInt64 = 0
    private(set) var latestPacketGapCount: UInt64 = 0

    var isRunning: Bool { output?.health(now: Date()).healthy == true }

    /// Reassert the virtual input route before a new dictation session.  After
    /// macOS rebuilds its audio graph, callbacks may keep running even though
    /// the virtual device route held by an existing
    /// client is no longer usable. Rebuilding after a short idle period keeps
    /// the first recording after reconnect/wake from inheriting that stale
    /// route, while consecutive recordings keep the warm engine.
    func prepareForRecording(rebuildAfterIdle idleSeconds: TimeInterval) throws -> (name: String, rebuilt: Bool) {
        guard let device = MicrophoneCoreAudio.virtualDevice() else {
            output?.stop(); output = nil; ring.reset()
            throw NSError(domain: "M5StopWatchMic", code: 3,
                          userInfo: [NSLocalizedDescriptionKey: "Virtual microphone is not installed: \(stopWatchVirtualMicrophoneName)"])
        }

        let now = Date()
        let outputHealth = output?.health(now: now)
        let packetIdle = lastPacketAt.map { now.timeIntervalSince($0) } ?? .infinity
        let needsRebuild = output == nil ||
            outputHealth?.healthy != true ||
            packetIdle >= idleSeconds

        if needsRebuild {
            let name = output == nil ? try start() : try restartOutput()
            prepareForStreamRestart()
            return (name, true)
        }

        if MicrophoneCoreAudio.defaultInput() != device.input {
            try MicrophoneCoreAudio.setDefaultInput(device.input)
        }
        return (device.name, false)
    }

    func start() throws -> String {
        if let outputDeviceName, output?.health(now: Date()).healthy == true { return outputDeviceName }
        if output != nil { return try restartOutput() }
        guard let device = MicrophoneCoreAudio.virtualDevice() else {
            throw NSError(domain: "M5StopWatchMic", code: 3,
                          userInfo: [NSLocalizedDescriptionKey: "Virtual microphone is not installed: \(stopWatchVirtualMicrophoneName)"])
        }
        ring.reset(); resetTracking()
        if previousInput == nil {
            previousInput = MicrophoneCoreAudio.defaultInput()
        }
        output = try VirtualMicrophoneOutput(ring: ring, device: device.output)
        do { try MicrophoneCoreAudio.setDefaultInput(device.input) }
        catch { output?.stop(); output = nil; throw error }
        outputDeviceName = device.name
        return device.name
    }

    func restartOutput() throws -> String {
        // Mute/dispose first, including when the virtual device has disappeared.
        output?.stop()
        output = nil
        ring.reset()
        guard let device = MicrophoneCoreAudio.virtualDevice() else {
            throw NSError(domain: "M5StopWatchMic", code: 3,
                          userInfo: [NSLocalizedDescriptionKey: "Virtual microphone is not installed: \(stopWatchVirtualMicrophoneName)"])
        }
        let replacement = try VirtualMicrophoneOutput(ring: ring, device: device.output)
        do {
            try MicrophoneCoreAudio.setDefaultInput(device.input)
        } catch {
            replacement.stop()
            throw error
        }
        output = replacement
        outputDeviceName = device.name
        return device.name
    }

#if DEBUG
    func stopOutputForTesting() {
        output?.stop()
    }
#endif

    func stop() {
        output?.stop(); output = nil
        ring.reset(); resetTracking()
        if let previousInput { try? MicrophoneCoreAudio.setDefaultInput(previousInput) }
        previousInput = nil; outputDeviceName = nil
    }

    func processAudioPacket(_ data: Data) -> Bool {
        let header = 14
        guard data.count >= header else { return false }
        let sequence = read16(data, 0)
        let sampleIndex = read32(data, 4)
        let sampleCount = Int(read16(data, 8))
        var predictor = Int(Int16(bitPattern: read16(data, 10)))
        var stepIndex = Int(data[12])
        let encodedCount = (max(0, sampleCount - 1) + 1) / 2
        guard data[2] == 1, sampleCount > 0, sampleCount <= 4096,
              stepIndex < imaStepTable.count, data.count == header + encodedCount else { return false }

        if let expectedSampleIndex, sampleIndex < expectedSampleIndex {
            self.expectedSequence = nil
            self.expectedSampleIndex = nil
            lostPacketCount = 0
        }
        if let expectedSequence, sequence != expectedSequence {
            latestPacketGapCount = UInt64(sequence &- expectedSequence)
            lostPacketCount += latestPacketGapCount
        } else {
            latestPacketGapCount = 0
        }
        expectedSequence = sequence &+ 1
        if let expectedSampleIndex, sampleIndex > expectedSampleIndex {
            let gap = sampleIndex - expectedSampleIndex
            if gap <= UInt32(microphoneSampleRate * 2) { ring.appendSilence(Int(gap)) }
        }
        expectedSampleIndex = sampleIndex &+ UInt32(sampleCount)

        predictor = max(-32768, min(32767, predictor))
        var pcm = Array(repeating: Int16(0), count: sampleCount)
        var squaredSampleSum = Double(predictor * predictor)
        pcm[0] = Int16(predictor)
        for offset in 1..<sampleCount {
            let codeOffset = offset - 1
            let packed = data[header + codeOffset / 2]
            let code = Int((codeOffset & 1) == 0 ? packed & 0x0f : packed >> 4)
            let step = imaStepTable[stepIndex]
            var difference = step >> 3
            if (code & 4) != 0 { difference += step }
            if (code & 2) != 0 { difference += step >> 1 }
            if (code & 1) != 0 { difference += step >> 2 }
            predictor += (code & 8) != 0 ? -difference : difference
            predictor = max(-32768, min(32767, predictor))
            stepIndex = max(0, min(88, stepIndex + imaIndexTable[code]))
            pcm[offset] = Int16(predictor)
            squaredSampleSum += Double(predictor * predictor)
        }
        packetCount += 1
        lastPacketAt = Date()
        lastDecodedRMS = sqrt(squaredSampleSum / Double(sampleCount)) / 32_768.0
        ring.append(pcm)
        return true
    }

    func statsDescription(_ data: Data) -> String? {
        guard data.count >= 20 else { return nil }
        lastStatsAt = Date()
        deviceStreaming = data[1] != 0
        deviceAudioSubscribed = data[2] != 0
        devicePacketsSent = read32(data, 8)
        devicePacketsDropped = read32(data, 12)
        if data.count >= 32, data[0] >= 4 {
            deviceNotifyError = Int32(bitPattern: read32(data, 20))
            deviceConsecutiveNotifyFailures = read32(data, 24)
            deviceFreeMbufs = read32(data, 28)
        } else {
            deviceNotifyError = 0
            deviceConsecutiveNotifyFailures = 0
            deviceFreeMbufs = 0
        }
        var description = "v\(data[0]) stream=\(deviceStreaming) rate=\(read32(data, 4)) sent=\(devicePacketsSent) dropped=\(devicePacketsDropped)"
        if data.count >= 32, data[0] >= 4 {
            description += " notify_error=\(deviceNotifyError) consecutive=\(deviceConsecutiveNotifyFailures) free_mbufs=\(deviceFreeMbufs)"
        }
        return description
    }

    func prepareForStreamRestart() {
        expectedSequence = nil
        expectedSampleIndex = nil
        packetCount = 0
        lostPacketCount = 0
        latestPacketGapCount = 0
        lastPacketAt = nil
        lastDecodedRMS = 0
    }

    func health(now: Date = Date()) -> MicrophonePipelineHealth {
        let outputHealth = output?.health(now: now)
        return MicrophonePipelineHealth(
            outputConfigured: output != nil,
            engineRunning: outputHealth?.engineRunning ?? false,
            outputHealthy: outputHealth?.healthy ?? false,
            outputRouteValid: outputHealth?.routeValid ?? false,
            outputRouteDescription: outputHealth?.routeDescription ?? "not configured",
            outputAge: outputHealth?.outputAge,
            renderAge: outputHealth?.renderAge,
            renderCount: outputHealth?.renderCount ?? 0,
            packetAge: lastPacketAt.map { now.timeIntervalSince($0) },
            packetCount: packetCount,
            lostPacketCount: lostPacketCount,
            latestPacketGapCount: latestPacketGapCount,
            decodedRMS: lastDecodedRMS,
            statsAge: lastStatsAt.map { now.timeIntervalSince($0) },
            deviceStreaming: deviceStreaming,
            deviceAudioSubscribed: deviceAudioSubscribed,
            devicePacketsSent: devicePacketsSent,
            devicePacketsDropped: devicePacketsDropped,
            deviceNotifyError: deviceNotifyError,
            deviceConsecutiveNotifyFailures: deviceConsecutiveNotifyFailures,
            deviceFreeMbufs: deviceFreeMbufs
        )
    }

    private func resetTracking() {
        expectedSequence = nil; expectedSampleIndex = nil; packetCount = 0; lostPacketCount = 0
        latestPacketGapCount = 0
        lastPacketAt = nil; lastStatsAt = nil; lastDecodedRMS = 0
        deviceStreaming = false; deviceAudioSubscribed = false
        devicePacketsSent = 0; devicePacketsDropped = 0
        deviceNotifyError = 0; deviceConsecutiveNotifyFailures = 0; deviceFreeMbufs = 0
    }
    private func read16(_ data: Data, _ offset: Int) -> UInt16 {
        UInt16(data[offset]) | (UInt16(data[offset + 1]) << 8)
    }
    private func read32(_ data: Data, _ offset: Int) -> UInt32 {
        UInt32(data[offset]) | (UInt32(data[offset + 1]) << 8) |
            (UInt32(data[offset + 2]) << 16) | (UInt32(data[offset + 3]) << 24)
    }
}
