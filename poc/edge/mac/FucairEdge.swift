// Edge control PoC, Mac side: a menu bar app. When the pointer is pushed past
// the screen edge facing the PC, the Mac's keyboard and mouse drive the PC;
// pushing back past the PC's Mac-facing edge returns control. Paused while
// the Mac is mirroring to the PC. Protocol: see windows/edge_receiver.cpp.
import AppKit
import CryptoKit
import Darwin

// MARK: - Configuration and logging

let home = NSHomeDirectory() + "/.fucairserver"
let keyPath = home + "/edge.key"

struct Config {
    var host = "Fucairserver-C8CB9E640D1B.local"
    var port: UInt16 = 7021
    var side = "left"            // where the PC sits relative to the Mac
    var scale = 1.0              // Mac points to PC pixels
    var display = "Eric-Mingle-Legion"
}

func loadConfig() -> Config {
    var c = Config()
    guard let text = try? String(contentsOfFile: home + "/edge.conf", encoding: .utf8) else { return c }
    for line in text.split(separator: "\n") {
        let kv = line.split(separator: "=", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
        guard kv.count == 2 else { continue }
        switch kv[0] {
        case "host": c.host = kv[1]
        case "port": c.port = UInt16(kv[1]) ?? c.port
        case "side": c.side = kv[1]
        case "scale": c.scale = Double(kv[1]) ?? c.scale
        case "display": c.display = kv[1]
        default: break
        }
    }
    return c
}

func saveConfig(_ c: Config) {
    let text = "host=\(c.host)\nport=\(c.port)\nside=\(c.side)\nscale=\(c.scale)\ndisplay=\(c.display)\n"
    try? text.write(toFile: home + "/edge.conf", atomically: true, encoding: .utf8)
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

var config = loadConfig()

// MARK: - Sockets

func connectTCP(_ host: String, _ port: UInt16) -> Int32? {
    var hints = addrinfo(ai_flags: 0, ai_family: AF_INET, ai_socktype: SOCK_STREAM, ai_protocol: IPPROTO_TCP,
                         ai_addrlen: 0, ai_canonname: nil, ai_addr: nil, ai_next: nil)
    var res: UnsafeMutablePointer<addrinfo>?
    guard getaddrinfo(host, String(port), &hints, &res) == 0, let ai = res else { return nil }
    defer { freeaddrinfo(res) }
    let fd = socket(ai.pointee.ai_family, ai.pointee.ai_socktype, ai.pointee.ai_protocol)
    guard fd >= 0 else { return nil }
    guard Darwin.connect(fd, ai.pointee.ai_addr, ai.pointee.ai_addrlen) == 0 else { close(fd); return nil }
    var one: Int32 = 1
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, 4)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, 4)
    return fd
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

// MARK: - Pairing

func pair(code: String) -> String? {
    guard let fd = connectTCP(config.host, config.port) else { return "连接不到 \(config.host):\(config.port)" }
    defer { close(fd) }
    let priv = Curve25519.KeyAgreement.PrivateKey()
    let macPub = [UInt8](priv.publicKey.rawRepresentation)
    guard writeAll(fd, Array("FPAIR".utf8) + macPub), let reply = readAll(fd, 37),
          Array(reply[0..<5]) == Array("FPAIR".utf8) else { return "拯救者没有响应配对" }
    let pcPub = Array(reply[5..<37])
    guard let peer = try? Curve25519.KeyAgreement.PublicKey(rawRepresentation: pcPub),
          let shared = try? priv.sharedSecretFromKeyAgreement(with: peer) else { return "密钥交换失败" }
    let sharedBytes = shared.withUnsafeBytes { Array($0) }
    let material = Array("fucair-edge-pair-v1".utf8) + sharedBytes + Array(code.utf8) + macPub + pcPub
    let key = Array(SHA256.hash(data: material))
    let symmetric = SymmetricKey(data: key)
    let confirm = Array(HMAC<SHA256>.authenticationCode(for: Array("mac-confirm".utf8), using: symmetric))
    guard writeAll(fd, confirm), let answer = readAll(fd, 32) else { return "拯救者断开了配对" }
    let expect = Array(HMAC<SHA256>.authenticationCode(for: Array("pc-confirm".utf8), using: symmetric))
    guard answer == expect else { return "配对码不对" }
    FileManager.default.createFile(atPath: keyPath, contents: Data(key), attributes: [.posixPermissions: 0o600])
    log("paired")
    return nil
}

// MARK: - Session link

enum Msg: UInt8 { case hello = 0, move = 1, button = 2, scroll = 3, key = 4, enter = 5, leave = 6, ping = 7, release = 8 }

final class Link {
    private var fd: Int32 = -1
    private var key: SymmetricKey?
    private var sendCounter: UInt64 = 0
    private var recvCounter: UInt64 = 0
    private let queue = DispatchQueue(label: "edge.link", qos: .userInteractive)
    private(set) var connected = false
    var onLeave: ((Double) -> Void)?
    var onDrop: (() -> Void)?

    func connect() -> Bool {
        guard let psk = FileManager.default.contents(atPath: keyPath), psk.count == 32,
              let fd = connectTCP(config.host, config.port) else { return false }
        var clientRandom = [UInt8](repeating: 0, count: 16)
        _ = SecRandomCopyBytes(kSecRandomDefault, 16, &clientRandom)
        guard writeAll(fd, Array("FEDG1".utf8) + clientRandom), let reply = readAll(fd, 21),
              Array(reply[0..<5]) == Array("FEDG1".utf8) else { close(fd); return false }
        let material = [UInt8](psk) + clientRandom + Array(reply[5..<21])
        queue.sync {
            self.fd = fd
            self.key = SymmetricKey(data: Array(SHA256.hash(data: material)))
            self.sendCounter = 0; self.recvCounter = 0
            self.connected = true
        }
        send([Msg.hello.rawValue])
        Thread { self.readLoop(fd) }.start()
        log("session connected to \(config.host)")
        return true
    }

    func send(_ plain: [UInt8]) {
        queue.async {
            guard self.connected, let key = self.key else { return }
            self.sendCounter += 1
            var nonce = [UInt8](repeating: 0, count: 12)
            for i in 0..<8 { nonce[4 + i] = UInt8(truncatingIfNeeded: self.sendCounter >> (8 * UInt64(i))) }
            guard let box = try? AES.GCM.seal(plain, using: key, nonce: AES.GCM.Nonce(data: nonce)) else { return }
            let body = nonce + [UInt8](box.ciphertext) + [UInt8](box.tag)
            let frame = [UInt8(body.count >> 8), UInt8(body.count & 0xff)] + body
            if !writeAll(self.fd, frame) { self.drop() }
        }
    }

    private func readLoop(_ fd: Int32) {
        while let head = readAll(fd, 2) {
            let len = Int(head[0]) << 8 | Int(head[1])
            guard len >= 29, let body = readAll(fd, len), let key = queue.sync(execute: { self.key }) else { break }
            let nonce = Array(body[0..<12])
            var counter: UInt64 = 0
            for i in (0..<8).reversed() { counter = counter << 8 | UInt64(nonce[4 + i]) }
            guard nonce[0] == 1, counter > recvCounter,
                  let box = try? AES.GCM.SealedBox(nonce: AES.GCM.Nonce(data: nonce),
                                                   ciphertext: body[12..<(len - 16)], tag: body[(len - 16)...]),
                  let plain = try? AES.GCM.open(box, using: key), !plain.isEmpty else { break }
            recvCounter = counter
            let p = [UInt8](plain)
            if p[0] == Msg.leave.rawValue, p.count >= 3 {
                let y = Double(UInt16(p[1]) << 8 | UInt16(p[2])) / 65535
                DispatchQueue.main.async { self.onLeave?(y) }
            }
        }
        queue.async { self.drop() }
    }

    private func drop() {
        guard connected else { return }
        connected = false
        close(fd); fd = -1; key = nil
        log("session dropped")
        DispatchQueue.main.async { self.onDrop?() }
    }
}

let link = Link()

// MARK: - Key mapping (macOS virtual key code -> Windows virtual key, extended)

let keyMap: [Int64: (UInt16, Bool)] = {
    var m: [Int64: (UInt16, Bool)] = [:]
    let letters: [(Int64, Character)] = [(0,"A"),(11,"B"),(8,"C"),(2,"D"),(14,"E"),(3,"F"),(5,"G"),(4,"H"),(34,"I"),(38,"J"),
        (40,"K"),(37,"L"),(46,"M"),(45,"N"),(31,"O"),(35,"P"),(12,"Q"),(15,"R"),(1,"S"),(17,"T"),(32,"U"),(9,"V"),(13,"W"),(7,"X"),(16,"Y"),(6,"Z")]
    for (code, ch) in letters { m[code] = (UInt16(ch.asciiValue!), false) }
    let digits: [(Int64, UInt16)] = [(29,0x30),(18,0x31),(19,0x32),(20,0x33),(21,0x34),(23,0x35),(22,0x36),(26,0x37),(28,0x38),(25,0x39)]
    for (code, vk) in digits { m[code] = (vk, false) }
    let rest: [(Int64, UInt16, Bool)] = [
        (24,0xBB,false),(27,0xBD,false),(33,0xDB,false),(30,0xDD,false),(41,0xBA,false),(39,0xDE,false),
        (42,0xDC,false),(43,0xBC,false),(47,0xBE,false),(44,0xBF,false),(50,0xC0,false),
        (36,0x0D,false),(48,0x09,false),(49,0x20,false),(51,0x08,false),(53,0x1B,false),
        (117,0x2E,true),(115,0x24,true),(119,0x23,true),(116,0x21,true),(121,0x22,true),
        (123,0x25,true),(126,0x26,true),(124,0x27,true),(125,0x28,true),
        (122,0x70,false),(120,0x71,false),(99,0x72,false),(118,0x73,false),(96,0x74,false),(97,0x75,false),
        (98,0x76,false),(100,0x77,false),(101,0x78,false),(109,0x79,false),(103,0x7A,false),(111,0x7B,false),
        (82,0x60,false),(83,0x61,false),(84,0x62,false),(85,0x63,false),(86,0x64,false),(87,0x65,false),
        (88,0x66,false),(89,0x67,false),(91,0x68,false),(92,0x69,false),(67,0x6A,false),(69,0x6B,false),
        (78,0x6D,false),(65,0x6E,false),(75,0x6F,true),(76,0x0D,true),
        // Mac habits on Windows: Command acts as Ctrl, Option as Alt, Control as the Windows key.
        (55,0xA2,false),(54,0xA3,true),(56,0xA0,false),(60,0xA1,false),(58,0xA4,false),(61,0xA5,true),
        (59,0x5B,true),(62,0x5C,true),(57,0x14,false)]
    for (code, vk, ext) in rest { m[code] = (vk, ext) }
    return m
}()

// Device-dependent modifier bits (NX_DEVICE*KEYMASK) per macOS key code.
let modifierBits: [Int64: UInt64] = [55: 0x08, 54: 0x10, 56: 0x02, 60: 0x04, 58: 0x20, 61: 0x40, 59: 0x01, 62: 0x2000]

// MARK: - Edge logic

var remote = false
var enabled = true
var edgeRect = CGRect.zero
var parkPoint = CGPoint.zero
var residual = (x: 0.0, y: 0.0)
var tapPort: CFMachPort?
var commandsDown = Set<Int64>()
var appSwitching = false

// A background app cannot hide the cursor through public API; this is the
// WindowServer connection property other software KVMs (e.g. Barrier) use.
typealias CGSConnectionID = Int32
@_silgen_name("_CGSDefaultConnection") func _CGSDefaultConnection() -> CGSConnectionID
@_silgen_name("CGSSetConnectionProperty")
func CGSSetConnectionProperty(_ cid: CGSConnectionID, _ target: CGSConnectionID, _ key: CFString, _ value: CFTypeRef) -> Int32
func allowBackgroundCursorHiding() {
    let cid = _CGSDefaultConnection()
    _ = CGSSetConnectionProperty(cid, cid, "SetsCursorInBackground" as CFString, kCFBooleanTrue)
}
let eventSource: CGEventSource? = {
    let s = CGEventSource(stateID: .combinedSessionState)
    s?.localEventsSuppressionInterval = 0
    return s
}()

func be16(_ v: Int) -> [UInt8] { let c = UInt16(bitPattern: Int16(clamping: v)); return [UInt8(c >> 8), UInt8(c & 0xff)] }

func displays() -> [CGRect] {
    var ids = [CGDirectDisplayID](repeating: 0, count: 16); var n: UInt32 = 0
    CGGetActiveDisplayList(16, &ids, &n)
    return ids.prefix(Int(n)).map { CGDisplayBounds($0) }
}

func mirroring() -> Bool { NSScreen.screens.contains { $0.localizedName == config.display } }

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
    // The PC sits to the left, so the cursor enters at the PC's right edge.
    link.send([Msg.enter.rawValue, UInt8(y >> 8), UInt8(y & 0xff), left ? 1 : 0])
    remote = true
    residual = (0, 0)
    commandsDown.removeAll(); appSwitching = false
    parkPoint = CGPoint(x: left ? rect.minX + 1 : rect.maxX - 2, y: loc.y)
    CGAssociateMouseAndMouseCursorPosition(0)
    CGDisplayHideCursor(CGMainDisplayID())
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
    log("control -> Mac")
}

func sendKey(_ code: Int64, down: Bool) {
    guard let (vk, ext) = keyMap[code] else { return }
    link.send([Msg.key.rawValue, UInt8(vk >> 8), UInt8(vk & 0xff), down ? 1 : 0, ext ? 1 : 0])
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
        // Keep the hidden Mac cursor parked while the PC has control.
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
        // Command-Tab switches apps on the Mac; on Windows that is Alt-Tab,
        // not Ctrl-Tab. Swap the held Command (sent as Ctrl) for Alt.
        if type == .keyDown, code == 48, !commandsDown.isEmpty, !appSwitching {
            for c in commandsDown { sendKey(c, down: false) }
            sendKey(58, down: true)
            appSwitching = true
        }
        sendKey(code, down: type == .keyDown)
    case .flagsChanged:
        let code = event.getIntegerValueField(.keyboardEventKeycode)
        if code == 57 { sendKey(57, down: true); sendKey(57, down: false) }  // Caps Lock toggles
        else if let bit = modifierBits[code] {
            let down = event.flags.rawValue & bit != 0
            if code == 55 || code == 54 {
                if down { commandsDown.insert(code) } else { commandsDown.remove(code) }
                if appSwitching {
                    if commandsDown.isEmpty { sendKey(58, down: false); appSwitching = false }
                    return nil
                }
            }
            sendKey(code, down: down)
        }
    default:
        return Unmanaged.passUnretained(event)
    }
    return nil
}

// MARK: - Menu bar app

final class App: NSObject, NSApplicationDelegate {
    var item: NSStatusItem!
    let status = NSMenuItem(title: "", action: nil, keyEquivalent: "")
    let toggle = NSMenuItem(title: "启用边缘控制", action: #selector(toggleEnabled), keyEquivalent: "")
    let leftItem = NSMenuItem(title: "拯救者在左边", action: #selector(setLeft), keyEquivalent: "")
    let rightItem = NSMenuItem(title: "拯救者在右边", action: #selector(setRight), keyEquivalent: "")

    func applicationDidFinishLaunching(_ note: Notification) {
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        let menu = NSMenu()
        for i in [status, NSMenuItem.separator(), toggle, leftItem, rightItem, NSMenuItem.separator()] { menu.addItem(i) }
        menu.addItem(NSMenuItem(title: "重新配对…", action: #selector(repair), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "退出", action: #selector(quit), keyEquivalent: "q"))
        for i in menu.items { i.target = self }
        item.menu = menu
        link.onLeave = { y in returnHere(y, notifyPC: false) }
        link.onDrop = { returnHere(0.5, notifyPC: false); self.refresh() }
        refresh()
        allowBackgroundCursorHiding()
        startTap()
        if FileManager.default.contents(atPath: keyPath)?.count != 32 { DispatchQueue.main.async { self.promptPairing() } }
        Thread {
            while true {
                if !link.connected, FileManager.default.contents(atPath: keyPath)?.count == 32, link.connect() {
                    DispatchQueue.main.async { self.refresh() }
                }
                Thread.sleep(forTimeInterval: 2)
                if link.connected { link.send([Msg.ping.rawValue]) }
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
        let mask = types.reduce(CGEventMask(0)) { $0 | (1 << $1.rawValue) }
        // HID level: ahead of macOS's own shortcuts (Command-Tab, Control-Space,
        // Mission Control), so they reach the PC instead of acting on the Mac.
        guard trusted, let port = CGEvent.tapCreate(tap: .cghidEventTap, place: .headInsertEventTap, options: .defaultTap,
                                                    eventsOfInterest: mask, callback: tapCallback, userInfo: nil) else {
            log("waiting for Accessibility / Input Monitoring permission")
            refresh(note: "需要授权：辅助功能、输入监控")
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { self.startTap() }
            return
        }
        tapPort = port
        CFRunLoopAddSource(CFRunLoopGetMain(), CFMachPortCreateRunLoopSource(nil, port, 0), .commonModes)
        CGEvent.tapEnable(tap: port, enable: true)
        log("event tap ready")
        refresh()
    }

    func refresh(note: String? = nil) {
        item.button?.title = remote ? "⇆●" : "⇆"
        status.title = note ?? (FileManager.default.contents(atPath: keyPath)?.count != 32 ? "未配对" :
                                link.connected ? "已连接 \(config.host)" : "未连接，自动重试中")
        toggle.state = enabled ? .on : .off
        leftItem.state = config.side == "left" ? .on : .off
        rightItem.state = config.side == "right" ? .on : .off
    }

    func promptPairing() {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "与拯救者配对"
        alert.informativeText = "输入拯救者上显示的 6 位配对码。配对一次后会记住，之后自动连接。"
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 200, height: 24))
        alert.accessoryView = field
        alert.addButton(withTitle: "配对"); alert.addButton(withTitle: "取消")
        alert.window.initialFirstResponder = field
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let code = field.stringValue.trimmingCharacters(in: .whitespaces)
        if let error = pair(code: code) {
            let e = NSAlert(); e.messageText = "配对失败"; e.informativeText = error; e.runModal()
        }
        refresh()
    }

    @objc func toggleEnabled() { enabled.toggle(); refresh() }
    @objc func setLeft() { config.side = "left"; saveConfig(config); refresh() }
    @objc func setRight() { config.side = "right"; saveConfig(config); refresh() }
    @objc func repair() { try? FileManager.default.removeItem(atPath: keyPath); promptPairing() }
    @objc func quit() { returnHere(0.5, notifyPC: true); NSApp.terminate(nil) }
}

try? FileManager.default.createDirectory(atPath: home, withIntermediateDirectories: true)
let app = NSApplication.shared
let delegate = App()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
