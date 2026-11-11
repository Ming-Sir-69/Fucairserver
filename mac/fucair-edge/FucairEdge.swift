// FucairEdge, the Mac side of Fucairserver edge control: a menu bar app.
// Push the pointer past the screen edge facing the PC and this Mac's keyboard
// and mouse drive the PC; push back past the PC's Mac-facing edge to return.
// While the PC has control, text produced on the Mac (for example by voice
// input) goes to the PC too. Also shares the clipboard. Paused while the Mac
// mirrors its screen to the PC. Protocol: src/edge_control.h.
import AppKit
import Carbon.HIToolbox
import CryptoKit
import Darwin
import IOKit.pwr_mgt

// MARK: - Configuration and logging

let home = NSHomeDirectory() + "/.fucairserver"
let keyPath = home + "/edge.key"
let idPath = home + "/edge-id"
let confPath = home + "/edge.conf"

struct Config {
    var pcId = ""                // paired PC, hex
    var pcName = ""
    var host = ""                // last address that worked
    var port: UInt16 = 7021
    var side = "left"            // where the PC sits relative to the Mac
    var scale = 1.0              // Mac points to PC pixels
    var receiverName = ""        // the PC's AirPlay name, for mirroring detection
    var captureInput = true      // route Mac text input to the PC while it has control
}

func loadConfig() -> Config {
    var c = Config()
    guard let text = try? String(contentsOfFile: confPath, encoding: .utf8) else { return c }
    for line in text.split(separator: "\n") {
        let kv = line.split(separator: "=", maxSplits: 1).map { String($0) }
        guard kv.count == 2 else { continue }
        switch kv[0] {
        case "pc_id": c.pcId = kv[1]
        case "pc_name": c.pcName = kv[1]
        case "host": c.host = kv[1]
        case "port": c.port = UInt16(kv[1]) ?? c.port
        case "side": c.side = kv[1] == "right" ? "right" : "left"
        case "scale": c.scale = Double(kv[1]) ?? c.scale
        case "receiver", "display": c.receiverName = kv[1]
        case "capture_input": c.captureInput = kv[1] != "0"
        default: break
        }
    }
    return c
}

func saveConfig(_ c: Config) {
    let text = "pc_id=\(c.pcId)\npc_name=\(c.pcName)\nhost=\(c.host)\nport=\(c.port)\nside=\(c.side)\n" +
               "scale=\(c.scale)\nreceiver=\(c.receiverName)\ncapture_input=\(c.captureInput ? 1 : 0)\n"
    try? text.write(toFile: confPath, atomically: true, encoding: .utf8)
}

let stamp: ISO8601DateFormatter = {
    let f = ISO8601DateFormatter(); f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]; return f
}()
func log(_ text: String) {
    let line = stamp.string(from: Date()) + " " + text + "\n"
    let url = URL(fileURLWithPath: home + "/edge.log")
    if let h = try? FileHandle(forWritingTo: url) { h.seekToEndOfFile(); h.write(line.data(using: .utf8)!); try? h.close() }
    else { try? line.write(to: url, atomically: true, encoding: .utf8) }
}

try? FileManager.default.createDirectory(atPath: home, withIntermediateDirectories: true)
var config = loadConfig()
// A key from the first prototype has no PC identity and cannot be used.
if config.pcId.isEmpty { try? FileManager.default.removeItem(atPath: keyPath) }

let macId: [UInt8] = {
    if let d = FileManager.default.contents(atPath: idPath), d.count == 16 { return [UInt8](d) }
    var id = [UInt8](repeating: 0, count: 16)
    _ = SecRandomCopyBytes(kSecRandomDefault, 16, &id)
    FileManager.default.createFile(atPath: idPath, contents: Data(id), attributes: [.posixPermissions: 0o600])
    return id
}()

func paired() -> Bool { !config.pcId.isEmpty && FileManager.default.contents(atPath: keyPath)?.count == 32 }

func hex(_ b: [UInt8]) -> String { b.map { String(format: "%02x", $0) }.joined() }

// MARK: - Sockets

func connectTCP(_ host: String, _ port: UInt16, timeout: Int = 5) -> Int32? {
    var hints = addrinfo(ai_flags: 0, ai_family: AF_INET, ai_socktype: SOCK_STREAM, ai_protocol: IPPROTO_TCP,
                         ai_addrlen: 0, ai_canonname: nil, ai_addr: nil, ai_next: nil)
    var res: UnsafeMutablePointer<addrinfo>?
    guard getaddrinfo(host, String(port), &hints, &res) == 0, let ai = res else { return nil }
    defer { freeaddrinfo(res) }
    let fd = socket(ai.pointee.ai_family, ai.pointee.ai_socktype, ai.pointee.ai_protocol)
    guard fd >= 0 else { return nil }
    var tv = timeval(tv_sec: timeout, tv_usec: 0)
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
    guard Darwin.connect(fd, ai.pointee.ai_addr, ai.pointee.ai_addrlen) == 0 else { close(fd); return nil }
    var one: Int32 = 1
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, 4)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, 4)
    return fd
}

func setReadTimeout(_ fd: Int32, _ seconds: Int) {
    var tv = timeval(tv_sec: seconds, tv_usec: 0)
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
}

func writeAll(_ fd: Int32, _ bytes: [UInt8]) -> Bool {
    var off = 0
    while off < bytes.count {
        let n = bytes[off...].withUnsafeBytes { write(fd, $0.baseAddress, $0.count) }
        if n <= 0 { return false }
        off += n
    }
    return true
}

func readAll(_ fd: Int32, _ count: Int) -> [UInt8]? {
    var out = [UInt8](repeating: 0, count: count)
    var off = 0
    while off < count {
        let n = out[off...].withUnsafeMutableBytes { read(fd, $0.baseAddress, $0.count) }
        if n <= 0 { return nil }
        off += n
    }
    return out
}

func sha(_ parts: [[UInt8]]) -> [UInt8] {
    var h = SHA256()
    for p in parts { h.update(data: p) }
    return Array(h.finalize())
}

// MARK: - Discovery (Bonjour)

struct FoundPC { var name: String; var host: String; var port: UInt16; var id: String }

final class Discovery: NSObject, NetServiceBrowserDelegate, NetServiceDelegate {
    private let browser = NetServiceBrowser()
    private var resolving: [NetService] = []
    private var found: [String: FoundPC] = [:]
    private let lock = NSLock()

    func start() {
        browser.delegate = self
        browser.searchForServices(ofType: "_fucair-edge._tcp.", inDomain: "local.")
    }
    func all() -> [FoundPC] { lock.lock(); defer { lock.unlock() }; return Array(found.values) }
    func lookup(_ id: String) -> FoundPC? { all().first { $0.id == id } }

    func netServiceBrowser(_ b: NetServiceBrowser, didFind s: NetService, moreComing: Bool) {
        resolving.append(s)
        s.delegate = self
        s.resolve(withTimeout: 10)
    }
    func netServiceBrowser(_ b: NetServiceBrowser, didRemove s: NetService, moreComing: Bool) {
        lock.lock(); found[s.name] = nil; lock.unlock()
    }
    func netServiceDidResolveAddress(_ s: NetService) {
        guard let host = s.hostName else { return }
        var id = ""
        if let txt = s.txtRecordData(), let v = NetService.dictionary(fromTXTRecord: txt)["id"] {
            id = String(data: v, encoding: .utf8) ?? ""
        }
        lock.lock(); found[s.name] = FoundPC(name: s.name, host: host, port: UInt16(s.port), id: id); lock.unlock()
        resolving.removeAll { $0 === s }
    }
    func netService(_ s: NetService, didNotResolve errorDict: [String: NSNumber]) { resolving.removeAll { $0 === s } }
}

let discovery = Discovery()

// MARK: - Pairing (numeric comparison, see src/edge_control.h)

enum PairResult { case paired, cancelled, failed(String) }

func pair(with pc: FoundPC, confirm: @escaping (String, String) -> Bool) -> PairResult {
    guard let fd = connectTCP(pc.host, pc.port) else { return .failed("连接不到 \(pc.name)") }
    defer { close(fd) }
    setReadTimeout(fd, 200)
    let priv = Curve25519.KeyAgreement.PrivateKey()
    let macPub = [UInt8](priv.publicKey.rawRepresentation)
    var name = Array((Host.current().localizedName ?? "Mac").utf8)
    if name.count > 200 { name = Array(name[0..<200]) }
    guard writeAll(fd, Array("FPAR2".utf8) + macPub + macId + [UInt8(name.count)] + name) else { return .failed("连接中断") }
    guard let head = readAll(fd, 5), head == Array("FPAR2".utf8),
          let body = readAll(fd, 81) else { return .failed("电脑没有接受配对请求。请确认电脑上的 Fucairserver 已打开键鼠跨屏，并且没有在和别的 Mac 配对。") }
    let pcPub = Array(body[0..<32]), pcId = Array(body[32..<48]), commit = Array(body[48..<80])
    guard let pcNameBytes = readAll(fd, Int(body[80])) else { return .failed("连接中断") }
    let pcName = String(decoding: pcNameBytes, as: UTF8.self)
    var macNonce = [UInt8](repeating: 0, count: 16)
    _ = SecRandomCopyBytes(kSecRandomDefault, 16, &macNonce)
    guard writeAll(fd, macNonce), let pcNonce = readAll(fd, 16) else { return .failed("连接中断") }
    guard sha([Array("fucair-commit".utf8), pcPub, pcNonce, macPub]) == commit else { return .failed("配对校验失败，已中止。") }
    let h = sha([Array("fucair-code".utf8), macPub, pcPub, macNonce, pcNonce])
    let n = (UInt32(h[0]) << 24 | UInt32(h[1]) << 16 | UInt32(h[2]) << 8 | UInt32(h[3])) % 1_000_000
    let code = String(format: "%03u %03u", n / 1000, n % 1000)

    var accepted = false
    DispatchQueue.main.sync { accepted = confirm(pcName.isEmpty ? pc.name : pcName, code) }
    guard writeAll(fd, [accepted ? 1 : 0]) else { return .failed("连接中断") }
    if !accepted { return .cancelled }
    guard let theirs = readAll(fd, 1) else { return .failed("电脑没有回应") }
    guard theirs[0] == 1 else { return .failed("电脑上没有确认配对。") }
    guard let peer = try? Curve25519.KeyAgreement.PublicKey(rawRepresentation: pcPub),
          let shared = try? priv.sharedSecretFromKeyAgreement(with: peer) else { return .failed("密钥交换失败") }
    let sharedBytes = shared.withUnsafeBytes { Array($0) }
    let key = sha([Array("fucair-edge-pair-v2".utf8), sharedBytes, macPub, pcPub, macNonce, pcNonce])
    let symmetric = SymmetricKey(data: key)
    let mine = Array(HMAC<SHA256>.authenticationCode(for: Array("mac-confirm".utf8), using: symmetric))
    guard writeAll(fd, mine), let answer = readAll(fd, 32),
          answer == Array(HMAC<SHA256>.authenticationCode(for: Array("pc-confirm".utf8), using: symmetric))
    else { return .failed("配对确认失败") }
    FileManager.default.createFile(atPath: keyPath, contents: Data(key), attributes: [.posixPermissions: 0o600])
    config.pcId = hex(pcId)
    config.pcName = pcName.isEmpty ? pc.name : pcName
    config.host = pc.host
    config.port = pc.port
    saveConfig(config)
    log("paired with \(config.pcName)")
    return .paired
}

// MARK: - Session link

enum Msg: UInt8 {
    case hello = 0, move = 1, button = 2, scroll = 3, key = 4, enter = 5, leave = 6, ping = 7, release = 8
    case clip = 9, config = 10, text = 11, media = 12
}

final class Link {
    private var fd: Int32 = -1
    private var key: SymmetricKey?
    private var sendCounter: UInt64 = 0
    private var recvCounter: UInt64 = 0
    private let queue = DispatchQueue(label: "edge.link", qos: .userInteractive)
    private(set) var connected = false
    private(set) var refused: UInt8 = 0      // 1 = PC forgot this Mac, 2 = turned off on the PC
    var onLeave: ((Double) -> Void)?
    var onDrop: (() -> Void)?
    var onConfig: ((Bool, String) -> Void)?
    var onClip: ((UInt8, [UInt8]) -> Void)?
    var onText: ((String) -> Void)?

    func connect(host: String, port: UInt16) -> Bool {
        guard let psk = FileManager.default.contents(atPath: keyPath), psk.count == 32,
              let fd = connectTCP(host, port) else { return false }
        setReadTimeout(fd, 10)
        var clientRandom = [UInt8](repeating: 0, count: 16)
        _ = SecRandomCopyBytes(kSecRandomDefault, 16, &clientRandom)
        guard writeAll(fd, Array("FEDG2".utf8) + macId + clientRandom), let head = readAll(fd, 5) else { close(fd); return false }
        if head == Array("FEDGX".utf8) {
            refused = readAll(fd, 1)?[0] ?? 2
            close(fd)
            return false
        }
        guard head == Array("FEDG2".utf8), let serverRandom = readAll(fd, 16) else { close(fd); return false }
        refused = 0
        setReadTimeout(fd, 0)    // liveness comes from ping replies, see stale()
        lastReceived = Date()
        let material = [UInt8](psk) + clientRandom + serverRandom
        queue.sync {
            self.fd = fd
            self.key = SymmetricKey(data: sha([material]))
            self.sendCounter = 0; self.recvCounter = 0
            self.connected = true
        }
        send([Msg.hello.rawValue])
        Thread { self.readLoop(fd) }.start()
        log("session connected to \(host)")
        return true
    }

    func send(_ plain: [UInt8]) {
        queue.async {
            guard self.connected, let key = self.key else { return }
            self.sendCounter += 1
            var nonce = [UInt8](repeating: 0, count: 12)
            for i in 0..<8 { nonce[4 + i] = UInt8(truncatingIfNeeded: self.sendCounter >> (8 * UInt64(i))) }
            guard let box = try? AES.GCM.seal(plain, using: key, nonce: AES.GCM.Nonce(data: nonce)) else { return }
            let len = 12 + box.ciphertext.count + 16
            let head: [UInt8] = [UInt8(len >> 24 & 0xff), UInt8(len >> 16 & 0xff), UInt8(len >> 8 & 0xff), UInt8(len & 0xff)]
            if !writeAll(self.fd, head + nonce + [UInt8](box.ciphertext) + [UInt8](box.tag)) { self.drop() }
        }
    }

    private var lastReceived = Date()
    // The PC answers every ping; silence means it went away (sleep, network change).
    var stale: Bool { queue.sync { connected && Date().timeIntervalSince(lastReceived) > 8 } }

    private func readLoop(_ fd: Int32) {
        while true {
            guard let head = readAll(fd, 4) else { break }
            queue.sync { lastReceived = Date() }
            let len = Int(head[0]) << 24 | Int(head[1]) << 16 | Int(head[2]) << 8 | Int(head[3])
            guard len >= 29, len <= 64 << 20, let body = readAll(fd, len), let key = queue.sync(execute: { self.key }) else { break }
            let nonce = Array(body[0..<12])
            var counter: UInt64 = 0
            for i in (0..<8).reversed() { counter = counter << 8 | UInt64(nonce[4 + i]) }
            guard nonce[0] == 1, counter > recvCounter,
                  let box = try? AES.GCM.SealedBox(nonce: AES.GCM.Nonce(data: nonce),
                                                   ciphertext: body[12..<(len - 16)], tag: body[(len - 16)...]),
                  let plain = try? AES.GCM.open(box, using: key), !plain.isEmpty else { break }
            recvCounter = counter
            let p = [UInt8](plain)
            switch p[0] {
            case Msg.leave.rawValue where p.count >= 3:
                let y = Double(UInt16(p[1]) << 8 | UInt16(p[2])) / 65535
                DispatchQueue.main.async { self.onLeave?(y) }
            case Msg.config.rawValue where p.count >= 2:
                let clip = p[1] & 1 != 0
                let name = String(decoding: p[2...], as: UTF8.self)
                DispatchQueue.main.async { self.onConfig?(clip, name) }
            case Msg.text.rawValue where p.count >= 2:
                let text = String(decoding: p[1...], as: UTF8.self)
                DispatchQueue.main.async { self.onText?(text) }
            case Msg.clip.rawValue where p.count >= 2:
                let type = p[1], data = Array(p[2...])
                DispatchQueue.main.async { self.onClip?(type, data) }
            default: break
            }
        }
        queue.async { self.drop() }
    }

    func disconnect() { queue.async { self.drop() } }

    private func drop() {
        guard connected else { return }
        connected = false
        shutdown(fd, SHUT_RDWR)   // also wakes the blocked reader
        close(fd); fd = -1; key = nil
        log("session dropped")
        DispatchQueue.main.async { self.onDrop?() }
    }
}

let link = Link()

// Device-dependent modifier bits (NX_DEVICE*KEYMASK) per macOS key code.
let modifierBits: [Int64: UInt64] = [55: 0x08, 54: 0x10, 56: 0x02, 60: 0x04, 58: 0x20, 61: 0x40, 59: 0x01, 62: 0x2000]

// MARK: - Input capture: Mac text input goes to the PC

// While the PC has control, keyboard focus moves with the pointer, as with
// Universal Control: an invisible field takes focus so nothing lands in Mac
// apps, and text that Mac input methods commit there (voice input) is typed
// on the PC. Ordinary keys never reach it; they go to the PC as key codes.
// The other direction: with the pointer on the Mac, text entered on the PC
// (for example its voice input) is typed here, at the Mac's focus.
func typeOnMac(_ text: String) {
    guard !remote else { return }
    let source = CGEventSource(stateID: .combinedSessionState)
    func post(_ units: [UniChar]) {
        guard !units.isEmpty else { return }
        for down in [true, false] {
            let e = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: down)
            units.withUnsafeBufferPointer { e?.keyboardSetUnicodeString(stringLength: units.count, unicodeString: $0.baseAddress) }
            e?.post(tap: .cgSessionEventTap)
        }
    }
    var chunk: [UniChar] = []
    for ch in text {
        if ch == "\n" {
            post(chunk); chunk.removeAll()
            for down in [true, false] { CGEvent(keyboardEventSource: source, virtualKey: 36, keyDown: down)?.post(tap: .cgSessionEventTap) }
            continue
        }
        let units = Array(String(ch).utf16)
        if chunk.count + units.count > 16 { post(chunk); chunk.removeAll() }
        chunk += units
    }
    post(chunk)
}

final class CaptureView: NSTextView {
    override func insertText(_ string: Any, replacementRange: NSRange) {
        let s = (string as? String) ?? (string as? NSAttributedString)?.string ?? ""
        if remote, !s.isEmpty { link.send([Msg.text.rawValue] + Array(s.utf8)) }
    }
    override func doCommand(by selector: Selector) {}
}

// A non-activating panel takes keyboard focus without making FucairEdge the
// active app, the way Spotlight-style launchers accept typing. macOS refuses
// to let a background app activate itself, but not this.
final class CapturePanel: NSPanel {
    override var canBecomeKey: Bool { true }
}

var capturePanel: CapturePanel?

func beginCapture() {
    guard config.captureInput else { return }
    if capturePanel == nil {
        let p = CapturePanel(contentRect: NSRect(x: 0, y: 0, width: 2, height: 2),
                             styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        p.isOpaque = false
        p.backgroundColor = .clear
        p.alphaValue = 0.01
        p.level = .statusBar
        p.ignoresMouseEvents = true
        p.hidesOnDeactivate = false
        p.becomesKeyOnlyIfNeeded = false
        p.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient, .ignoresCycle]
        p.contentView = CaptureView(frame: NSRect(x: 0, y: 0, width: 2, height: 2))
        capturePanel = p
    }
    guard let p = capturePanel else { return }
    // macOS can keep an input source per app. Carry over the one in use, so
    // the user's input method (and its voice input) stays available here.
    let source = TISCopyCurrentKeyboardInputSource().takeRetainedValue()
    // Beside the parked pointer, away from the Dock and hot corners (Cocoa y grows upwards).
    let top = NSScreen.screens.first?.frame.maxY ?? 0
    p.setFrameOrigin(NSPoint(x: parkPoint.x + (config.side == "left" ? 4 : -6), y: top - parkPoint.y))
    p.orderFrontRegardless()
    p.makeKey()
    p.makeFirstResponder(p.contentView)
    TISSelectInputSource(source)
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
        guard remote else { return }
        var focused: AnyObject?
        AXUIElementCopyAttributeValue(AXUIElementCreateSystemWide(), kAXFocusedApplicationAttribute as CFString, &focused)
        var pid: pid_t = 0
        if let app = focused { AXUIElementGetPid(app as! AXUIElement, &pid) }
        log("input capture: panel key=\(p.isKeyWindow) self=\(pid == ProcessInfo.processInfo.processIdentifier) " +
            "source=\(inputSourceID(source)) now=\(inputSourceID(TISCopyCurrentKeyboardInputSource().takeRetainedValue()))")
    }
}

func inputSourceID(_ source: TISInputSource) -> String {
    guard let raw = TISGetInputSourceProperty(source, kTISPropertyInputSourceID) else { return "?" }
    return Unmanaged<CFString>.fromOpaque(raw).takeUnretainedValue() as String
}

func endCapture() {
    guard let p = capturePanel, p.isVisible else { return }
    p.orderOut(nil)
}

// MARK: - Edge logic

var remote = false
var enabled = true
var clipboardShared = false
var edgeRect = CGRect.zero
var parkPoint = CGPoint.zero
var residual = (x: 0.0, y: 0.0)
var tapPort: CFMachPort?
var awake: IOPMAssertionID = 0

// A background app cannot hide the cursor through public API; this is the
// WindowServer connection property Universal Control and other KVMs use.
typealias CGSConnectionID = Int32
@_silgen_name("_CGSDefaultConnection") func _CGSDefaultConnection() -> CGSConnectionID
@_silgen_name("CGSSetConnectionProperty")
func CGSSetConnectionProperty(_ cid: CGSConnectionID, _ target: CGSConnectionID, _ key: CFString, _ value: CFTypeRef) -> Int32
func allowBackgroundCursorHiding() {
    let cid = _CGSDefaultConnection()
    _ = CGSSetConnectionProperty(cid, cid, "SetsCursorInBackground" as CFString, kCFBooleanTrue)
}

func be16(_ v: Int) -> [UInt8] { let c = UInt16(bitPattern: Int16(clamping: v)); return [UInt8(c >> 8), UInt8(c & 0xff)] }

func displays() -> [CGRect] {
    var ids = [CGDirectDisplayID](repeating: 0, count: 16); var n: UInt32 = 0
    CGGetActiveDisplayList(16, &ids, &n)
    return ids.prefix(Int(n)).map { CGDisplayBounds($0) }
}

func mirroring() -> Bool { !config.receiverName.isEmpty && NSScreen.screens.contains { $0.localizedName == config.receiverName } }

func tryEnter(_ loc: CGPoint, _ dx: Double) -> Bool {
    guard enabled, link.connected, !mirroring() else { return false }
    let all = displays()
    let left = config.side == "left"
    let edgeX = left ? all.map(\.minX).min() ?? 0 : all.map(\.maxX).max() ?? 0
    guard left ? (loc.x <= edgeX + 0.5 && dx < 0) : (loc.x >= edgeX - 1.5 && dx > 0) else { return false }
    guard let rect = all.first(where: { $0.minY <= loc.y && loc.y < $0.maxY && (left ? $0.minX == edgeX : $0.maxX == edgeX) })
    else { return false }
    edgeRect = rect
    let yfrac = max(0, min(1, (loc.y - rect.minY) / rect.height))
    let y = UInt16(yfrac * 65535)
    let caps = CGEventSource.flagsState(.hidSystemState).contains(.maskAlphaShift)
    // The PC sits to the left, so the pointer enters at the PC's right edge.
    link.send([Msg.enter.rawValue, UInt8(y >> 8), UInt8(y & 0xff), left ? 1 : 0, caps ? 1 : 0])
    remote = true
    residual = (0, 0)
    parkPoint = CGPoint(x: left ? rect.minX + 1 : rect.maxX - 2, y: loc.y)
    CGAssociateMouseAndMouseCursorPosition(0)
    CGDisplayHideCursor(CGMainDisplayID())
    IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep as CFString,
                                IOPMAssertionLevel(kIOPMAssertionLevelOn), "FucairEdge: the PC has control" as CFString, &awake)
    beginCapture()
    log("control -> PC")
    return true
}

func returnHere(_ yfrac: Double, notifyPC: Bool) {
    guard remote else { return }
    if notifyPC { link.send([Msg.release.rawValue]) }
    remote = false
    let left = config.side == "left"
    let p = CGPoint(x: left ? edgeRect.minX + 3 : edgeRect.maxX - 4, y: edgeRect.minY + yfrac * (edgeRect.height - 1))
    CGWarpMouseCursorPosition(p)
    CGAssociateMouseAndMouseCursorPosition(1)
    CGDisplayShowCursor(CGMainDisplayID())
    if awake != 0 { IOPMAssertionRelease(awake); awake = 0 }
    endCapture()
    log("control -> Mac")
}

func sendKey(_ code: Int64, _ down: Bool) {
    link.send([Msg.key.rawValue, UInt8(code >> 8 & 0xff), UInt8(code & 0xff), down ? 1 : 0])
}

let tapCallback: CGEventTapCallBack = { _, type, event, _ in
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        if let port = tapPort { CGEvent.tapEnable(tap: port, enable: true) }
        return Unmanaged.passUnretained(event)
    }
    let isMove = type == .mouseMoved || type == .leftMouseDragged || type == .rightMouseDragged || type == .otherMouseDragged
    if !remote {
        if isMove && tryEnter(event.location, event.getDoubleValueField(.mouseEventDeltaX)) { return nil }
        return Unmanaged.passUnretained(event)
    }
    switch type {
    case _ where isMove:
        residual.x += event.getDoubleValueField(.mouseEventDeltaX) * config.scale
        residual.y += event.getDoubleValueField(.mouseEventDeltaY) * config.scale
        let dx = Int(residual.x.rounded(.towardZero)), dy = Int(residual.y.rounded(.towardZero))
        residual.x -= Double(dx); residual.y -= Double(dy)
        if dx != 0 || dy != 0 { link.send([Msg.move.rawValue] + be16(dx) + be16(dy)) }
        CGWarpMouseCursorPosition(parkPoint)
    case .leftMouseDown, .leftMouseUp, .rightMouseDown, .rightMouseUp, .otherMouseDown, .otherMouseUp:
        let button: UInt8 = (type == .leftMouseDown || type == .leftMouseUp) ? 0 : (type == .rightMouseDown || type == .rightMouseUp) ? 1 : 2
        let down = type == .leftMouseDown || type == .rightMouseDown || type == .otherMouseDown
        link.send([Msg.button.rawValue, button, down ? 1 : 0])
    case .scrollWheel:
        let continuous = event.getIntegerValueField(.scrollWheelEventIsContinuous) != 0
        let v = continuous ? event.getDoubleValueField(.scrollWheelEventPointDeltaAxis1) * 3
                           : Double(event.getIntegerValueField(.scrollWheelEventDeltaAxis1)) * 120
        let h = continuous ? event.getDoubleValueField(.scrollWheelEventPointDeltaAxis2) * 3
                           : Double(event.getIntegerValueField(.scrollWheelEventDeltaAxis2)) * 120
        link.send([Msg.scroll.rawValue] + be16(Int(-h)) + be16(Int(v)))
    case .keyDown, .keyUp:
        let code = event.getIntegerValueField(.keyboardEventKeycode)
        let flags = event.flags
        if type == .keyDown, code == 53, flags.contains(.maskControl), flags.contains(.maskAlternate), flags.contains(.maskCommand) {
            returnHere(0.5, notifyPC: true)
            return nil
        }
        sendKey(code, type == .keyDown)
    case .flagsChanged:
        let code = event.getIntegerValueField(.keyboardEventKeycode)
        if code == 57 { sendKey(57, true) }                       // each event is one Caps Lock toggle
        else if code == 63 { sendKey(63, event.flags.contains(.maskSecondaryFn)) }
        else if let bit = modifierBits[code] { sendKey(code, event.flags.rawValue & bit != 0) }
    default:
        // Media and volume keys arrive as system-defined events (NX_SYSDEFINED, subtype 8).
        if type.rawValue == 14, let ns = NSEvent(cgEvent: event), ns.subtype.rawValue == 8 {
            let data1 = ns.data1
            let state = (data1 & 0xFF00) >> 8
            link.send([Msg.media.rawValue, UInt8((data1 & 0xFFFF0000) >> 16 & 0xff), state == 0xA ? 1 : 0])
            return nil
        }
        return Unmanaged.passUnretained(event)
    }
    return nil
}

// MARK: - Clipboard

var lastChangeCount = NSPasteboard.general.changeCount

func pollClipboard() {
    let pb = NSPasteboard.general
    guard pb.changeCount != lastChangeCount else { return }
    lastChangeCount = pb.changeCount
    guard link.connected, clipboardShared else { return }
    let types = pb.types ?? []
    let privateTypes = ["org.nspasteboard.ConcealedType", "org.nspasteboard.TransientType", "org.nspasteboard.AutoGeneratedType"]
    if types.contains(where: { privateTypes.contains($0.rawValue) }) { return }
    var message: [UInt8]?
    if let s = pb.string(forType: .string) {
        message = [Msg.clip.rawValue, 1] + Array(s.utf8)
    } else if let png = pb.data(forType: .png) {
        message = [Msg.clip.rawValue, 2] + [UInt8](png)
    } else if let tiff = pb.data(forType: .tiff), let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) {
        message = [Msg.clip.rawValue, 2] + [UInt8](png)
    }
    // Other clipboard sync tools (and our own writes) can repost the same content.
    guard let m = message, m.count <= 48 << 20 else { return }
    let digest = sha([m])
    if digest == lastClipDigest { return }
    lastClipDigest = digest
    link.send(m)
}

var lastClipDigest: [UInt8] = []

func applyClipboard(_ type: UInt8, _ data: [UInt8]) {
    guard clipboardShared else { return }
    let pb = NSPasteboard.general
    switch type {
    case 1:
        pb.clearContents()
        pb.setString(String(decoding: data, as: UTF8.self), forType: .string)
    case 2, 3:
        guard let image = NSImage(data: Data(data)) else { return }
        pb.clearContents()
        if type == 2 { pb.setData(Data(data), forType: .png) }
        else if let tiff = image.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff),
                let png = rep.representation(using: .png, properties: [:]) { pb.setData(png, forType: .png) }
        if let tiff = image.tiffRepresentation { pb.setData(tiff, forType: .tiff) }
    default: return
    }
    lastChangeCount = pb.changeCount
    lastClipDigest = sha([[Msg.clip.rawValue, type] + data])
}

// MARK: - Menu bar app

final class App: NSObject, NSApplicationDelegate {
    var item: NSStatusItem!
    let status = NSMenuItem(title: "", action: nil, keyEquivalent: "")
    let toggle = NSMenuItem(title: "启用键鼠跨屏", action: #selector(toggleEnabled), keyEquivalent: "")
    let leftItem = NSMenuItem(title: "电脑在 Mac 左边", action: #selector(setLeft), keyEquivalent: "")
    let rightItem = NSMenuItem(title: "电脑在 Mac 右边", action: #selector(setRight), keyEquivalent: "")
    let captureItem = NSMenuItem(title: "控制电脑时，Mac 上的语音输入也发到电脑", action: #selector(toggleCapture), keyEquivalent: "")
    let pairItem = NSMenuItem(title: "与电脑配对…", action: #selector(pairMenu), keyEquivalent: "")
    var note: String?
    var pairing = false

    func applicationDidFinishLaunching(_ n: Notification) {
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        let menu = NSMenu()
        for i in [status, NSMenuItem.separator(), toggle, leftItem, rightItem, captureItem, NSMenuItem.separator(), pairItem] { menu.addItem(i) }
        menu.addItem(NSMenuItem(title: "退出", action: #selector(quit), keyEquivalent: "q"))
        for i in menu.items { i.target = self }
        item.menu = menu
        link.onLeave = { y in returnHere(y, notifyPC: false) }
        link.onDrop = { returnHere(0.5, notifyPC: false); clipboardShared = false; self.refresh() }
        link.onConfig = { clip, name in
            clipboardShared = clip
            if !name.isEmpty, name != config.receiverName { config.receiverName = name; saveConfig(config) }
            self.refresh()
        }
        link.onClip = { type, data in applyClipboard(type, data) }
        link.onText = { text in typeOnMac(text) }
        discovery.start()
        refresh()
        allowBackgroundCursorHiding()
        startTap()
        Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { _ in pollClipboard() }
        Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { _ in self.refresh() }
        if !paired() {
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { if !paired() { self.pairMenu() } }
        }
        Thread {
            while true {
                var wait = 2.0
                if !link.connected, paired() {
                    let found = discovery.lookup(config.pcId)
                    let host = found?.host ?? config.host, port = found?.port ?? config.port
                    if !host.isEmpty, link.connect(host: host, port: port) {
                        if host != config.host || port != config.port {
                            DispatchQueue.main.async { config.host = host; config.port = port; saveConfig(config) }
                        }
                    } else if link.refused == 1 {
                        log("the PC no longer knows this Mac; pairing again is needed")
                        try? FileManager.default.removeItem(atPath: keyPath)
                        DispatchQueue.main.async { self.note = "电脑已移除这台 Mac，请重新配对"; config.pcId = ""; saveConfig(config) }
                    } else if link.refused == 2 {
                        wait = 10
                    }
                    DispatchQueue.main.async { self.refresh() }
                }
                Thread.sleep(forTimeInterval: wait)
                if link.stale { log("no reply from the PC"); link.disconnect() }
                else if link.connected { link.send([Msg.ping.rawValue]) }
            }
        }.start()
    }

    var prompted = false

    func startTap() {
        let trusted = AXIsProcessTrustedWithOptions([kAXTrustedCheckOptionPrompt.takeUnretainedValue(): !prompted] as CFDictionary)
        if !prompted, !CGPreflightListenEventAccess() { CGRequestListenEventAccess() }
        prompted = true
        let types: [CGEventType] = [.mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged, .leftMouseDown, .leftMouseUp,
                                    .rightMouseDown, .rightMouseUp, .otherMouseDown, .otherMouseUp, .scrollWheel, .keyDown, .keyUp, .flagsChanged]
        let mask = types.reduce(CGEventMask(1 << 14)) { $0 | (1 << $1.rawValue) }
        // HID level: ahead of macOS's own shortcuts (Command-Tab, Control-Space,
        // Mission Control), so they reach the PC instead of acting on the Mac.
        guard trusted, let port = CGEvent.tapCreate(tap: .cghidEventTap, place: .headInsertEventTap, options: .defaultTap,
                                                    eventsOfInterest: mask, callback: tapCallback, userInfo: nil) else {
            if note == nil { log("waiting for Accessibility / Input Monitoring permission") }
            note = "需要授权：辅助功能、输入监控"
            refresh()
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { self.startTap() }
            return
        }
        note = nil
        tapPort = port
        CFRunLoopAddSource(CFRunLoopGetMain(), CFMachPortCreateRunLoopSource(nil, port, 0), .commonModes)
        CGEvent.tapEnable(tap: port, enable: true)
        log("event tap ready")
        refresh()
    }

    func refresh() {
        item.button?.title = remote ? "⇆●" : "⇆"
        let name = config.pcName.isEmpty ? "电脑" : config.pcName
        if let note = note { status.title = note }
        else if !paired() { status.title = "未配对：选择“与电脑配对…”" }
        else if link.connected { status.title = remote ? "正在控制 \(name)" : mirroring() ? "已连接 \(name)（镜像中，暂停）" : "已连接 \(name)" }
        else if link.refused == 2 { status.title = "\(name) 上的键鼠跨屏已关闭" }
        else { status.title = "正在连接 \(name)…" }
        toggle.state = enabled ? .on : .off
        leftItem.state = config.side == "left" ? .on : .off
        rightItem.state = config.side == "right" ? .on : .off
        captureItem.state = config.captureInput ? .on : .off
        pairItem.title = paired() ? "重新与电脑配对…" : "与电脑配对…"
    }

    @objc func pairMenu() {
        guard !pairing else { return }
        NSApp.activate(ignoringOtherApps: true)
        let pcs = discovery.all().filter { !$0.id.isEmpty }
        var target: FoundPC?
        if pcs.count == 1 {
            target = pcs[0]
        } else {
            let alert = NSAlert()
            alert.messageText = "与电脑配对"
            if pcs.isEmpty {
                alert.informativeText = "没有在网络上找到打开了 Fucairserver 的电脑。请确认两台设备在同一网络，或输入电脑的地址。"
                let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
                field.stringValue = config.host
                alert.accessoryView = field
                alert.addButton(withTitle: "继续"); alert.addButton(withTitle: "取消")
                guard alert.runModal() == .alertFirstButtonReturn else { return }
                let host = field.stringValue.trimmingCharacters(in: .whitespaces)
                guard !host.isEmpty else { return }
                target = FoundPC(name: host, host: host, port: 7021, id: "")
            } else {
                alert.informativeText = "选择要配对的电脑："
                let popup = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 260, height: 26))
                popup.addItems(withTitles: pcs.map(\.name))
                alert.accessoryView = popup
                alert.addButton(withTitle: "继续"); alert.addButton(withTitle: "取消")
                guard alert.runModal() == .alertFirstButtonReturn else { return }
                target = pcs[popup.indexOfSelectedItem]
            }
        }
        guard let pc = target else { return }
        pairing = true
        link.disconnect()
        note = "正在与 \(pc.name) 配对…"
        refresh()
        Thread {
            let result = pair(with: pc) { pcName, code in
                NSApp.activate(ignoringOtherApps: true)
                let a = NSAlert()
                a.messageText = "与“\(pcName)”配对"
                a.informativeText = "请确认电脑上显示的配对码与下面一致：\n\n\(code)\n\n一致就点“配对”，电脑上也请点“是”。配对后，这台 Mac 可以用键盘和鼠标控制那台电脑，并共享剪贴板。"
                a.addButton(withTitle: "配对"); a.addButton(withTitle: "取消")
                return a.runModal() == .alertFirstButtonReturn
            }
            DispatchQueue.main.async {
                self.pairing = false
                self.note = nil
                if case .failed(let why) = result {
                    let e = NSAlert(); e.messageText = "配对没有完成"; e.informativeText = why; e.runModal()
                }
                self.refresh()
            }
        }.start()
    }

    @objc func toggleEnabled() { enabled.toggle(); if !enabled { returnHere(0.5, notifyPC: true) }; refresh() }
    @objc func setLeft() { config.side = "left"; saveConfig(config); refresh() }
    @objc func setRight() { config.side = "right"; saveConfig(config); refresh() }
    @objc func toggleCapture() { config.captureInput.toggle(); saveConfig(config); refresh() }
    @objc func quit() { returnHere(0.5, notifyPC: true); NSApp.terminate(nil) }
}

let app = NSApplication.shared
let delegate = App()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
