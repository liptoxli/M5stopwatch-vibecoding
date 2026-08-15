import AVFoundation
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

private final class MicrophoneAudioRing {
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

private enum MicrophoneCoreAudio {
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
        return value?.takeUnretainedValue() as String?
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
        if let input = allDevices.first(where: { name(of: $0)?.localizedCaseInsensitiveCompare(stopWatchVirtualMicrophoneName) == .orderedSame }),
           let output = device(uid: "M5StopWatchMic_2_UID") {
            return (input, output, stopWatchVirtualMicrophoneName)
        }
        if let blackHole = allDevices.first(where: { name(of: $0)?.localizedCaseInsensitiveCompare("BlackHole 2ch") == .orderedSame }) {
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

private final class VirtualMicrophoneOutput {
    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode!

    init(ring: MicrophoneAudioRing, device: AudioDeviceID) throws {
        guard let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: microphoneSampleRate,
                                         channels: 1, interleaved: false) else {
            throw NSError(domain: "M5StopWatchMic", code: 1, userInfo: [NSLocalizedDescriptionKey: "Cannot create audio format"])
        }
        source = AVAudioSourceNode(format: format) { [weak ring] _, _, frameCount, list in
            let buffers = UnsafeMutableAudioBufferListPointer(list)
            guard let pointer = buffers.first?.mData?.assumingMemoryBound(to: Float.self) else { return noErr }
            ring?.render(into: pointer, frames: Int(frameCount))
            for index in 1..<buffers.count {
                buffers[index].mData?.assumingMemoryBound(to: Float.self).update(from: pointer, count: Int(frameCount))
            }
            return noErr
        }
        engine.attach(source)
        engine.connect(source, to: engine.mainMixerNode, format: format)
        guard let audioUnit = engine.outputNode.audioUnit else {
            throw NSError(domain: "M5StopWatchMic", code: 2, userInfo: [NSLocalizedDescriptionKey: "Cannot access audio output"])
        }
        var value = device
        let status = AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global,
                                          0, &value, UInt32(MemoryLayout<AudioDeviceID>.size))
        guard status == noErr else {
            throw NSError(domain: "M5StopWatchMic", code: Int(status), userInfo: [NSLocalizedDescriptionKey: "Cannot open virtual microphone output"])
        }
        engine.prepare()
        try engine.start()
    }

    func stop() { engine.stop() }
}

final class StopWatchMicrophonePipeline {
    private let ring = MicrophoneAudioRing()
    private var output: VirtualMicrophoneOutput?
    private var previousInput: AudioDeviceID?
    private var expectedSequence: UInt16?
    private var expectedSampleIndex: UInt32?
    private(set) var outputDeviceName: String?
    private(set) var packetCount: UInt64 = 0
    private(set) var lostPacketCount: UInt64 = 0

    var isRunning: Bool { output != nil }

    func start() throws -> String {
        if let outputDeviceName, output != nil { return outputDeviceName }
        guard let device = MicrophoneCoreAudio.virtualDevice() else {
            throw NSError(domain: "M5StopWatchMic", code: 3,
                          userInfo: [NSLocalizedDescriptionKey: "Virtual microphone is not installed: \(stopWatchVirtualMicrophoneName)"])
        }
        ring.reset(); resetTracking()
        previousInput = MicrophoneCoreAudio.defaultInput()
        output = try VirtualMicrophoneOutput(ring: ring, device: device.output)
        do { try MicrophoneCoreAudio.setDefaultInput(device.input) }
        catch { output?.stop(); output = nil; throw error }
        outputDeviceName = device.name
        return device.name
    }

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

        if let expectedSequence, sequence != expectedSequence { lostPacketCount += UInt64(sequence &- expectedSequence) }
        expectedSequence = sequence &+ 1
        if let expectedSampleIndex, sampleIndex > expectedSampleIndex {
            let gap = sampleIndex - expectedSampleIndex
            if gap <= UInt32(microphoneSampleRate * 2) { ring.appendSilence(Int(gap)) }
        }
        expectedSampleIndex = sampleIndex &+ UInt32(sampleCount)

        predictor = max(-32768, min(32767, predictor))
        var pcm = Array(repeating: Int16(0), count: sampleCount)
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
        }
        packetCount += 1
        ring.append(pcm)
        return true
    }

    func statsDescription(_ data: Data) -> String? {
        guard data.count >= 20 else { return nil }
        return "v\(data[0]) stream=\(data[1] != 0) rate=\(read32(data, 4)) sent=\(read32(data, 8)) dropped=\(read32(data, 12))"
    }

    private func resetTracking() {
        expectedSequence = nil; expectedSampleIndex = nil; packetCount = 0; lostPacketCount = 0
    }
    private func read16(_ data: Data, _ offset: Int) -> UInt16 {
        UInt16(data[offset]) | (UInt16(data[offset + 1]) << 8)
    }
    private func read32(_ data: Data, _ offset: Int) -> UInt32 {
        UInt32(data[offset]) | (UInt32(data[offset + 1]) << 8) |
            (UInt32(data[offset + 2]) << 16) | (UInt32(data[offset + 3]) << 24)
    }
}
