// Keeps the Mac's sound output when screen mirroring to a Fucairserver
// receiver starts. macOS moves the default output to the receiver's AirPlay
// device as mirroring activates; this switches back to the previous output.
// A later, deliberate choice of the receiver in the Sound menu is respected:
// only a switch within `window` seconds of the device appearing is undone.
import CoreAudio
import Foundation

let target = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "Eric-Mingle-Legion"
let window: TimeInterval = 15
let system = AudioObjectID(kAudioObjectSystemObject)

func addr(_ s: AudioObjectPropertySelector, _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: s, mScope: scope, mElement: kAudioObjectPropertyElementMain)
}
let stamp: ISO8601DateFormatter = {
    let f = ISO8601DateFormatter()
    f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    return f
}()
func log(_ text: String) {
    let line = stamp.string(from: Date()) + " " + text + "\n"
    let url = URL(fileURLWithPath: NSHomeDirectory() + "/.fucairserver/keep-mac-audio.log")
    if let h = try? FileHandle(forWritingTo: url) { h.seekToEndOfFile(); h.write(line.data(using: .utf8)!); try? h.close() }
    else { try? line.write(to: url, atomically: true, encoding: .utf8) }
}
func devices() -> [AudioDeviceID] {
    var a = addr(kAudioHardwarePropertyDevices); var size: UInt32 = 0
    AudioObjectGetPropertyDataSize(system, &a, 0, nil, &size)
    var ids = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.size)
    AudioObjectGetPropertyData(system, &a, 0, nil, &size, &ids)
    return ids
}
func name(_ id: AudioDeviceID) -> String {
    var a = addr(kAudioObjectPropertyName); var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
    var value: Unmanaged<CFString>? = nil
    guard AudioObjectGetPropertyData(id, &a, 0, nil, &size, &value) == noErr, let v = value else { return "" }
    return v.takeRetainedValue() as String
}
func isTarget(_ id: AudioDeviceID) -> Bool {
    var a = addr(kAudioDevicePropertyTransportType); var t: UInt32 = 0; var size = UInt32(4)
    AudioObjectGetPropertyData(id, &a, 0, nil, &size, &t)
    return t == kAudioDeviceTransportTypeAirPlay && name(id) == target
}
func defaultOutput() -> AudioDeviceID {
    var a = addr(kAudioHardwarePropertyDefaultOutputDevice); var id = AudioDeviceID(0); var size = UInt32(4)
    AudioObjectGetPropertyData(system, &a, 0, nil, &size, &id); return id
}
func setOutput(_ id: AudioDeviceID) -> OSStatus {
    var dev = id
    var a = addr(kAudioHardwarePropertyDefaultOutputDevice)
    let status = AudioObjectSetPropertyData(system, &a, 0, nil, 4, &dev)
    var b = addr(kAudioHardwarePropertyDefaultSystemOutputDevice)
    _ = AudioObjectSetPropertyData(system, &b, 0, nil, 4, &dev)
    return status
}

var known = Set(devices())
var appeared: [AudioDeviceID: Date] = [:]
var previous = defaultOutput()
if isTarget(previous) { previous = 0 }

// Notifications for the mirroring switch arrive hundreds of milliseconds
// late on a throttled background queue; handle them at interactive priority.
let queue = DispatchQueue(label: "keep-mac-audio", qos: .userInteractive)

func refreshDevices() {
    let now = Set(devices())
    for id in now.subtracting(known) where isTarget(id) { appeared[id] = Date() }
    for id in known.subtracting(now) { appeared[id] = nil }
    known = now
}

// Event driven only: both the device-list and default-output notifications
// run this check, so whichever CoreAudio delivers first restores the output.
func check() {
    let current = defaultOutput()
    guard isTarget(current) else {
        if current != 0 { previous = current }
        return
    }
    if appeared[current] == nil && !known.contains(current) { refreshDevices() }
    guard let since = appeared[current], Date().timeIntervalSince(since) <= window else { return }
    appeared[current] = .distantPast
    guard previous != 0, Set(devices()).contains(previous) else { log("mirroring took the output; no previous output to restore"); return }
    let status = setOutput(previous)
    let ms = Int(Date().timeIntervalSince(since) * 1000)
    log("mirroring to \(target) took the output; restored \(name(previous)) status=\(status) \(ms)ms after the device appeared")
}

// Timing trace for tuning: active only while ~/.fucairserver/keep-mac-audio.trace exists.
let traceFlag = NSHomeDirectory() + "/.fucairserver/keep-mac-audio.trace"
func trace(_ event: String) {
    guard FileManager.default.fileExists(atPath: traceFlag) else { return }
    let current = defaultOutput()
    var t: UInt32 = 0; var size = UInt32(4)
    var a = addr(kAudioDevicePropertyTransportType)
    AudioObjectGetPropertyData(current, &a, 0, nil, &size, &t)
    let transport = withUnsafeBytes(of: t.bigEndian) { String(bytes: Array($0), encoding: .ascii) ?? "?" }
    log("trace \(event): default=\(current) [\(transport)] '\(name(current))' listed=\(Set(devices()).contains(current)) target=\(isTarget(current))")
}

var listDevices = addr(kAudioHardwarePropertyDevices)
AudioObjectAddPropertyListenerBlock(system, &listDevices, queue) { _, _ in trace("devices"); refreshDevices(); check() }
var defaultDevice = addr(kAudioHardwarePropertyDefaultOutputDevice)
AudioObjectAddPropertyListenerBlock(system, &defaultDevice, queue) { _, _ in trace("default"); check() }
var systemDevice = addr(kAudioHardwarePropertyDefaultSystemOutputDevice)
AudioObjectAddPropertyListenerBlock(system, &systemDevice, queue) { _, _ in trace("system-default") }
log("started; watching \(target); current output \(name(defaultOutput()))")
dispatchMain()
