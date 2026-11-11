// Edge control PoC, Windows side: receives encrypted keyboard and mouse
// events from the Mac and injects them as local input. When the cursor
// returns to the Mac-facing edge, control is handed back to the Mac.
//
// Pairing (once): "FPAIR"+X25519 public key each way; both derive
// key = SHA-256("fucair-edge-pair-v1" || shared || code || mac pub || pc pub)
// and exchange HMAC confirmations. The 6-digit code is shown here only.
// Sessions: handshake "FEDG1"+16 random bytes each way, then frames
// [u16 big-endian length][12-byte nonce][AES-256-GCM ciphertext][16-byte tag].
// Session key = SHA-256(paired key || client random || server random).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using Key = std::array<uint8_t, 32>;

enum : uint8_t {
    kHello = 0, kMove = 1, kButton = 2, kScroll = 3, kKey = 4,
    kEnter = 5, kLeave = 6, kPing = 7, kRelease = 8
};

FILE* g_logfile = nullptr;

void log(const char* fmt, ...) {
    SYSTEMTIME t; GetLocalTime(&t);
    char line[1024];
    int n = std::snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args; va_start(args, fmt); std::vsnprintf(line + n, sizeof(line) - n, fmt, args); va_end(args);
    std::printf("%s\n", line); std::fflush(stdout);
    if (g_logfile) { std::fprintf(g_logfile, "%s\n", line); std::fflush(g_logfile); }
}

std::string key_path() {
    char appdata[MAX_PATH] = {};
    GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    return std::string(appdata) + "\\Fucairserver\\edge-paired.key";
}

bool load_key(Key& key) {
    std::ifstream in(key_path(), std::ios::binary);
    return in.read(reinterpret_cast<char*>(key.data()), key.size()) && in.gcount() == 32;
}

bool save_key(const Key& key) {
    const auto path = key_path();
    CreateDirectoryA(path.substr(0, path.rfind('\\')).c_str(), nullptr);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(key.data()), key.size());
    return static_cast<bool>(out);
}

void random_bytes(uint8_t* p, ULONG n) { BCryptGenRandom(nullptr, p, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG); }

bool sha256(const std::vector<uint8_t>& data, Key& out) {
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data.data()),
                      ULONG(data.size()), out.data(), 32) == 0;
}

bool hmac(const Key& key, const char* label, Key& out) {
    return BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, const_cast<PUCHAR>(key.data()), 32,
                      (PUCHAR)label, ULONG(std::strlen(label)), out.data(), 32) == 0;
}

class Cipher {
public:
    ~Cipher() { if (key_) BCryptDestroyKey(key_); if (alg_) BCryptCloseAlgorithmProvider(alg_, 0); }
    bool init(const Key& key) {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
        if (BCryptSetProperty(alg_, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) return false;
        return BCryptGenerateSymmetricKey(alg_, &key_, nullptr, 0, (PUCHAR)key.data(), 32, 0) == 0;
    }
    bool seal(const uint8_t nonce[12], const std::vector<uint8_t>& plain, std::vector<uint8_t>& out) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
        uint8_t tag[16];
        info.pbNonce = const_cast<PUCHAR>(nonce); info.cbNonce = 12;
        info.pbTag = tag; info.cbTag = 16;
        out.resize(plain.size());
        ULONG written = 0;
        if (BCryptEncrypt(key_, (PUCHAR)plain.data(), ULONG(plain.size()), &info, nullptr, 0,
                          out.data(), ULONG(out.size()), &written, 0) != 0) return false;
        out.insert(out.end(), tag, tag + 16);
        return true;
    }
    bool open(const uint8_t nonce[12], const uint8_t* data, size_t len, std::vector<uint8_t>& plain) {
        if (len < 16) return false;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = const_cast<PUCHAR>(nonce); info.cbNonce = 12;
        info.pbTag = const_cast<PUCHAR>(data + len - 16); info.cbTag = 16;
        plain.resize(len - 16);
        ULONG written = 0;
        return BCryptDecrypt(key_, const_cast<PUCHAR>(data), ULONG(len - 16), &info, nullptr, 0,
                             plain.data(), ULONG(plain.size()), &written, 0) == 0;
    }
private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
};

bool recv_all(SOCKET s, void* buf, int len) {
    auto* p = static_cast<char*>(buf);
    while (len > 0) { const int n = recv(s, p, len, 0); if (n <= 0) return false; p += n; len -= n; }
    return true;
}

bool send_all(SOCKET s, const void* buf, int len) {
    auto* p = static_cast<const char*>(buf);
    while (len > 0) { const int n = send(s, p, len, 0); if (n <= 0) return false; p += n; len -= n; }
    return true;
}

void make_nonce(uint8_t direction, uint64_t counter, uint8_t nonce[12]) {
    std::memset(nonce, 0, 12);
    nonce[0] = direction;
    for (int i = 0; i < 8; ++i) nonce[4 + i] = uint8_t(counter >> (8 * i));
}

uint64_t nonce_counter(const uint8_t nonce[12]) {
    uint64_t c = 0;
    for (int i = 7; i >= 0; --i) c = (c << 8) | nonce[4 + i];
    return c;
}

// ---- Pairing --------------------------------------------------------------

struct Pairing {
    std::string code;
    int failures = 0;
    void new_code() {
        uint32_t r = 0; random_bytes(reinterpret_cast<uint8_t*>(&r), 4);
        char buf[8]; std::snprintf(buf, sizeof(buf), "%06u", r % 1000000);
        code = buf; failures = 0;
        log("pairing code issued");
        std::printf("\n    配对码：%s   （在 Mac 上输入）\n\n", code.c_str()); std::fflush(stdout);
        std::thread([c = code] {
            const std::wstring text = L"在 Mac 的 FucairEdge 中输入配对码：\n\n        " +
                                      std::wstring(c.begin(), c.end());
            MessageBoxW(nullptr, text.c_str(), L"Fucairserver 键鼠配对",
                        MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        }).detach();
    }
};

bool x25519(const uint8_t peer_pub[32], uint8_t our_pub[32], uint8_t shared[32]) {
    EVP_PKEY* ours = EVP_PKEY_Q_keygen(nullptr, nullptr, "X25519");
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer_pub, 32);
    bool ok = ours && peer;
    size_t len = 32;
    if (ok) ok = EVP_PKEY_get_raw_public_key(ours, our_pub, &len) == 1 && len == 32;
    EVP_PKEY_CTX* ctx = ok ? EVP_PKEY_CTX_new(ours, nullptr) : nullptr;
    len = 32;
    if (ok) ok = ctx && EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_derive_set_peer(ctx, peer) == 1 &&
                 EVP_PKEY_derive(ctx, shared, &len) == 1 && len == 32;
    EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(peer); EVP_PKEY_free(ours);
    return ok;
}

void pair(SOCKET s, const uint8_t mac_pub[32], Pairing& pairing, Key& paired, bool& has_key) {
    if (has_key && pairing.code.empty()) { log("pairing request ignored: already paired (start with --pair to re-pair)"); return; }
    if (pairing.code.empty()) pairing.new_code();
    uint8_t pc_pub[32], shared[32];
    if (!x25519(mac_pub, pc_pub, shared)) { log("x25519 failed"); return; }
    uint8_t reply[37]; std::memcpy(reply, "FPAIR", 5); std::memcpy(reply + 5, pc_pub, 32);
    if (!send_all(s, reply, 37)) return;

    std::vector<uint8_t> material;
    const char* label = "fucair-edge-pair-v1";
    material.insert(material.end(), label, label + std::strlen(label));
    material.insert(material.end(), shared, shared + 32);
    material.insert(material.end(), pairing.code.begin(), pairing.code.end());
    material.insert(material.end(), mac_pub, mac_pub + 32);
    material.insert(material.end(), pc_pub, pc_pub + 32);
    Key key{}, expect{}, confirm{};
    sha256(material, key);
    hmac(key, "mac-confirm", expect);
    uint8_t got[32];
    if (!recv_all(s, got, 32)) return;
    if (std::memcmp(got, expect.data(), 32) != 0) {
        uint8_t zero[32] = {}; send_all(s, zero, 32);
        if (++pairing.failures >= 3) { log("3 wrong codes; issuing a new one"); pairing.new_code(); }
        else log("wrong pairing code (%d/3)", pairing.failures);
        return;
    }
    hmac(key, "pc-confirm", confirm);
    if (!send_all(s, confirm.data(), 32) || !save_key(key)) return;
    paired = key; has_key = true; pairing.code.clear();
    log("paired with the Mac; key saved to %s", key_path().c_str());
}

// ---- Cursor hiding ---------------------------------------------------------

// While the Mac has control, the PC pointer would otherwise sit visibly at the
// edge. A background process cannot hide it with ShowCursor, so swap every
// system cursor for a transparent one and restore the user's scheme when the
// pointer comes back or the PC's own mouse moves it.
class CursorHider {
public:
    void hide() {
        std::lock_guard<std::mutex> lock(mu_);
        if (hidden_) return;
        static const DWORD ids[] = {32512, 32513, 32514, 32515, 32516, 32642, 32643, 32644,
                                    32645, 32646, 32648, 32649, 32650, 32651};
        static const std::vector<uint8_t> and_mask(32 * 4, 0xFF), xor_mask(32 * 4, 0x00);
        for (DWORD id : ids) {
            HCURSOR blank = CreateCursor(GetModuleHandleW(nullptr), 0, 0, 32, 32, and_mask.data(), xor_mask.data());
            if (blank && !SetSystemCursor(blank, id)) DestroyCursor(blank);
        }
        GetCursorPos(&park_);
        hidden_ = true;
    }
    void show() {
        std::lock_guard<std::mutex> lock(mu_);
        if (!hidden_) return;
        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
        hidden_ = false;
    }
    // Startup / exit: undo a hide left over from a previous run.
    void reset() { SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0); hidden_ = false; }
    // Called periodically: local mouse movement brings the pointer back.
    void poll() {
        POINT p;
        bool moved;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!hidden_ || !GetCursorPos(&p)) return;
            moved = p.x != park_.x || p.y != park_.y;
        }
        if (moved) { show(); log("local mouse moved; cursor shown"); }
    }
private:
    std::mutex mu_;
    bool hidden_ = false;
    POINT park_{};
};

CursorHider g_cursor;

BOOL WINAPI on_console_close(DWORD) { g_cursor.reset(); return FALSE; }

// ---- Input injection -----------------------------------------------------

class Injector {
public:
    // from_right: the Mac sits to the right, so the cursor enters at the right edge.
    void enter(double yfrac, bool from_right) {
        refresh_desktop();
        from_right_ = from_right;
        x_ = from_right ? left_ + width_ - 1 : left_;
        y_ = top_ + yfrac * (height_ - 1);
        active_ = true;
        g_cursor.show();
        move_to();
        log("enter %s edge at y=%.0f", from_right ? "right" : "left", y_);
    }
    // Returns true when the cursor leaves through the Mac-facing edge.
    bool move(int dx, int dy, double& yfrac) {
        if (!active_) return false;
        const double nx = x_ + dx;
        const bool out = from_right_ ? (nx > left_ + width_ - 1 && dx > 0) : (nx < left_ && dx < 0);
        if (out) {
            yfrac = (y_ - top_) / std::max(1, height_ - 1);
            deactivate();
            log("leave at y=%.0f", y_);
            return true;
        }
        x_ = std::clamp(nx, double(left_), double(left_ + width_ - 1));
        y_ = std::clamp(y_ + dy, double(top_), double(top_ + height_ - 1));
        move_to();
        return false;
    }
    void button(uint8_t which, bool down) {
        if (!active_ || which > 2) return;
        static const DWORD downs[] = {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN};
        static const DWORD ups[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};
        send_button(which, down ? downs[which] : ups[which]);
        if (down) buttons_.insert(which); else buttons_.erase(which);
    }
    void scroll(int dx, int dy) {
        if (!active_) return;
        if (dy) { INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_WHEEL; in.mi.mouseData = DWORD(dy); SendInput(1, &in, sizeof(in)); }
        if (dx) { INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_HWHEEL; in.mi.mouseData = DWORD(dx); SendInput(1, &in, sizeof(in)); }
    }
    void key(uint16_t vk, bool down, bool extended) {
        if (!active_) return;
        send_key(vk, down, extended);
        if (down) keys_.insert({vk, extended}); else keys_.erase({vk, extended});
    }
    // Lift anything still held so nothing sticks after a handover or drop.
    void deactivate() {
        for (const auto& k : keys_) send_key(k.first, false, k.second);
        static const DWORD ups[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};
        for (auto b : buttons_) send_button(b, ups[b]);
        keys_.clear(); buttons_.clear();
        if (active_) g_cursor.hide();
        active_ = false;
    }
private:
    static void send_key(uint16_t vk, bool down, bool extended) {
        INPUT in{}; in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
        SendInput(1, &in, sizeof(in));
    }
    static void send_button(uint8_t, DWORD flags) {
        INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = flags;
        SendInput(1, &in, sizeof(in));
    }
    void refresh_desktop() {
        left_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
        top_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width_ = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height_ = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    void move_to() {
        INPUT in{}; in.type = INPUT_MOUSE;
        in.mi.dx = LONG((x_ - left_) * 65535.0 / std::max(1, width_ - 1));
        in.mi.dy = LONG((y_ - top_) * 65535.0 / std::max(1, height_ - 1));
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        SendInput(1, &in, sizeof(in));
    }
    int left_ = 0, top_ = 0, width_ = 1, height_ = 1;
    double x_ = 0, y_ = 0;
    bool active_ = false, from_right_ = false;
    std::set<std::pair<uint16_t, bool>> keys_;
    std::set<uint8_t> buttons_;
};

int16_t rd16(const uint8_t* p) { return int16_t(uint16_t(p[0]) << 8 | p[1]); }

void session(SOCKET s, const uint8_t client_random[16], const Key& paired, Injector& input) {
    uint8_t reply[21];
    std::memcpy(reply, "FEDG1", 5);
    random_bytes(reply + 5, 16);
    if (!send_all(s, reply, 21)) return;

    std::vector<uint8_t> material(paired.begin(), paired.end());
    material.insert(material.end(), client_random, client_random + 16);
    material.insert(material.end(), reply + 5, reply + 21);
    Key key{};
    Cipher cipher;
    if (!sha256(material, key) || !cipher.init(key)) { log("crypto init failed"); return; }

    uint64_t recv_counter = 0, send_counter = 0;
    bool authenticated = false;
    auto send_msg = [&](const std::vector<uint8_t>& plain) {
        uint8_t nonce[12]; make_nonce(1, ++send_counter, nonce);
        std::vector<uint8_t> sealed;
        if (!cipher.seal(nonce, plain, sealed)) return false;
        const uint16_t len = uint16_t(12 + sealed.size());
        uint8_t head[2] = {uint8_t(len >> 8), uint8_t(len)};
        return send_all(s, head, 2) && send_all(s, nonce, 12) && send_all(s, sealed.data(), int(sealed.size()));
    };

    std::vector<uint8_t> frame, plain;
    for (;;) {
        uint8_t head[2];
        if (!recv_all(s, head, 2)) break;
        const int len = head[0] << 8 | head[1];
        if (len < 12 + 16 + 1) break;
        frame.resize(len);
        if (!recv_all(s, frame.data(), len)) break;
        const uint64_t counter = nonce_counter(frame.data());
        if (frame[0] != 0 || counter <= recv_counter ||
            !cipher.open(frame.data(), frame.data() + 12, len - 12, plain) || plain.empty()) {
            log("rejected frame (not paired with this PC, or replay)");
            break;
        }
        recv_counter = counter;
        if (!authenticated && plain[0] != kHello) { log("first frame was not hello"); break; }
        const uint8_t* p = plain.data() + 1;
        switch (plain[0]) {
            case kHello: authenticated = true; log("mac connected"); break;
            case kEnter: if (plain.size() >= 4) input.enter(uint16_t(p[0] << 8 | p[1]) / 65535.0, p[2] != 0); break;
            case kMove: if (plain.size() >= 5) {
                double yfrac = 0;
                if (input.move(rd16(p), rd16(p + 2), yfrac)) {
                    const uint16_t y = uint16_t(std::clamp(yfrac, 0.0, 1.0) * 65535);
                    if (!send_msg({kLeave, uint8_t(y >> 8), uint8_t(y)})) goto done;
                }
            } break;
            case kButton: if (plain.size() >= 3) input.button(p[0], p[1] != 0); break;
            case kScroll: if (plain.size() >= 5) input.scroll(rd16(p), rd16(p + 2)); break;
            case kKey: if (plain.size() >= 5) input.key(uint16_t(p[0] << 8 | p[1]), p[2] != 0, p[3] != 0); break;
            case kRelease: input.deactivate(); log("released by mac"); break;
            default: break;
        }
    }
done:
    input.deactivate();
}

} // namespace

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);
    unsigned short port = 7021;
    bool force_pair = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pair") == 0) force_pair = true;
        else if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc) g_logfile = std::fopen(argv[++i], "a");
        else port = static_cast<unsigned short>(std::atoi(argv[i]));
    }
    g_cursor.reset();
    SetConsoleCtrlHandler(on_console_close, TRUE);
    std::thread([] {
        for (;;) { g_cursor.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    }).detach();
    Key paired{};
    bool has_key = load_key(paired);
    Pairing pairing;
    if (!has_key || force_pair) pairing.new_code();
    else log("paired key found; waiting for the Mac");

    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port); addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listener, 1) != 0) {
        log("cannot listen on %u", port); return 1;
    }
    log("edge receiver listening on %u", port);
    Injector input;
    for (;;) {
        sockaddr_in peer{}; int plen = sizeof(peer);
        SOCKET s = accept(listener, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (s == INVALID_SOCKET) continue;
        BOOL nodelay = TRUE; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        char ip[INET_ADDRSTRLEN] = {}; inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        uint8_t head[5];
        if (recv_all(s, head, 5)) {
            if (std::memcmp(head, "FPAIR", 5) == 0) {
                uint8_t mac_pub[32];
                log("pairing request from %s", ip);
                if (recv_all(s, mac_pub, 32)) pair(s, mac_pub, pairing, paired, has_key);
            } else if (std::memcmp(head, "FEDG1", 5) == 0) {
                uint8_t client_random[16];
                if (!has_key) log("session from %s refused: not paired yet", ip);
                else if (recv_all(s, client_random, 16)) { log("session from %s", ip); session(s, client_random, paired, input); }
            }
        }
        closesocket(s);
    }
}
