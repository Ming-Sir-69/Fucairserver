#include "edge_control.h"

#include "log.h"
#include "ui_text.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <objbase.h>
#include <wincodec.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace fucairserver::edge {
namespace {

using Bytes = std::vector<uint8_t>;
using Key = std::array<uint8_t, 32>;

enum : uint8_t {
    kHello = 0, kMove = 1, kButton = 2, kScroll = 3, kKey = 4, kEnter = 5, kLeave = 6,
    kPing = 7, kRelease = 8, kClip = 9, kConfigMsg = 10, kText = 11, kMedia = 12,
};
enum : uint8_t { kClipText = 1, kClipPng = 2, kClipBmp = 3 };
constexpr uint32_t kMaxFrame = 64u << 20;
constexpr size_t kMaxClip = 48u << 20;

// ---- Small helpers ----------------------------------------------------------

std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

bool unhex(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != n * 2) return false;
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (size_t i = 0; i < n; ++i) {
        const int hi = v(s[2 * i]), lo = v(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    return true;
}

void random_bytes(uint8_t* p, size_t n) { BCryptGenRandom(nullptr, p, ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG); }

Key sha256(const Bytes& data) {
    Key out{};
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data.data()), ULONG(data.size()), out.data(), 32);
    return out;
}

Key hmac(const Key& key, const char* label) {
    Key out{};
    BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, const_cast<PUCHAR>(key.data()), 32,
               (PUCHAR)label, ULONG(std::strlen(label)), out.data(), 32);
    return out;
}

void append(Bytes& b, const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
void append(Bytes& b, const char* s) { append(b, s, std::strlen(s)); }

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}

std::string narrow(const wchar_t* w, size_t n) {
    if (!n) return {};
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w, int(n), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, int(n), s.data(), int(s.size()), nullptr, nullptr);
    return s;
}

class Cipher {
public:
    ~Cipher() { if (key_) BCryptDestroyKey(key_); if (alg_) BCryptCloseAlgorithmProvider(alg_, 0); }
    bool init(const Key& key) {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
        if (BCryptSetProperty(alg_, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) return false;
        return BCryptGenerateSymmetricKey(alg_, &key_, nullptr, 0, (PUCHAR)key.data(), 32, 0) == 0;
    }
    bool seal(const uint8_t nonce[12], const Bytes& plain, Bytes& out) {
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
    bool open(const uint8_t nonce[12], const uint8_t* data, size_t len, Bytes& plain) {
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

bool recv_all(SOCKET s, void* buf, size_t len) {
    auto* p = static_cast<char*>(buf);
    while (len > 0) {
        const int n = recv(s, p, int(std::min<size_t>(len, 1 << 20)), 0);
        if (n <= 0) return false;
        p += n; len -= size_t(n);
    }
    return true;
}

bool send_all(SOCKET s, const void* buf, size_t len) {
    auto* p = static_cast<const char*>(buf);
    while (len > 0) {
        const int n = send(s, p, int(std::min<size_t>(len, 1 << 20)), 0);
        if (n <= 0) return false;
        p += n; len -= size_t(n);
    }
    return true;
}

void set_timeout(SOCKET s, DWORD ms) {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
}

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

Bytes protect(const std::string& text) {
    DATA_BLOB in{DWORD(text.size()), (BYTE*)text.data()}, out{};
    if (!CryptProtectData(&in, L"Fucairserver edge devices", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    Bytes b(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return b;
}

std::string unprotect(const Bytes& data) {
    DATA_BLOB in{DWORD(data.size()), (BYTE*)data.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string s((const char*)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
}

// ---- Cursor hiding -----------------------------------------------------------

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
    void poll() {
        POINT p;
        bool moved;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!hidden_ || !GetCursorPos(&p)) return;
            moved = p.x != park_.x || p.y != park_.y;
        }
        if (moved) show();
    }
private:
    std::mutex mu_;
    bool hidden_ = false;
    POINT park_{};
};

// ---- Input injection -----------------------------------------------------------

void send_key(uint16_t vk, bool ext, bool down) {
    INPUT in{}; in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(in));
}

void type_text(const std::string& utf8) {
    const std::wstring w = widen(utf8);
    std::vector<INPUT> inputs;
    for (wchar_t c : w) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            INPUT d{}; d.type = INPUT_KEYBOARD; d.ki.wVk = VK_RETURN;
            INPUT u = d; u.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(d); inputs.push_back(u);
            continue;
        }
        INPUT d{}; d.type = INPUT_KEYBOARD; d.ki.wScan = c; d.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT u = d; u.ki.dwFlags |= KEYEVENTF_KEYUP;
        inputs.push_back(d); inputs.push_back(u);
    }
    if (!inputs.empty()) SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
}

// The secure desktop (lock screen, UAC) cannot receive injected input.
bool input_desktop_usable() {
    HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (!desk) return false;
    wchar_t name[64] = {};
    DWORD needed = 0;
    const bool ok = GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name), &needed) && _wcsicmp(name, L"Default") == 0;
    CloseDesktop(desk);
    return ok;
}

class Injector {
public:
    Injector() : keys_([](uint16_t vk, bool ext, bool down) { send_key(vk, ext, down); }) {}

    void set_keys(const KeyConfig& k) { keys_.set_config(k); }
    bool active() const { return active_; }

    void enter(double yfrac, bool from_right, bool mac_caps) {
        refresh_desktop();
        from_right_ = from_right;
        x_ = from_right ? left_ + width_ - 1 : left_;
        y_ = top_ + yfrac * (height_ - 1);
        active_ = true;
        cursor_.show();
        move_to();
        if (((GetKeyState(VK_CAPITAL) & 1) != 0) != mac_caps) { send_key(VK_CAPITAL, false, true); send_key(VK_CAPITAL, false, false); }
    }
    bool move(int dx, int dy, double& yfrac) {
        if (!active_) return false;
        const double nx = x_ + dx;
        if (from_right_ ? (nx > left_ + width_ - 1 && dx > 0) : (nx < left_ && dx < 0)) {
            yfrac = (y_ - top_) / std::max(1, height_ - 1);
            deactivate();
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
        INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = down ? downs[which] : ups[which];
        SendInput(1, &in, sizeof(in));
        if (down) buttons_ |= 1u << which; else buttons_ &= ~(1u << which);
    }
    void scroll(int dx, int dy) {
        if (!active_) return;
        if (dy) { INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_WHEEL; in.mi.mouseData = DWORD(dy); SendInput(1, &in, sizeof(in)); }
        if (dx) { INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_HWHEEL; in.mi.mouseData = DWORD(dx); SendInput(1, &in, sizeof(in)); }
    }
    void key(uint16_t mac, bool down) { if (active_) keys_.key(mac, down); }
    void text(const std::string& utf8) { if (active_) type_text(utf8); }
    void media(uint8_t code, bool down) {
        if (!active_) return;
        // macOS NX_KEYTYPE_* codes.
        uint16_t vk = 0;
        switch (code) {
            case 0: vk = VK_VOLUME_UP; break;
            case 1: vk = VK_VOLUME_DOWN; break;
            case 7: vk = VK_VOLUME_MUTE; break;
            case 16: vk = VK_MEDIA_PLAY_PAUSE; break;
            case 17: case 19: vk = VK_MEDIA_NEXT_TRACK; break;
            case 18: case 20: vk = VK_MEDIA_PREV_TRACK; break;
            default: return;
        }
        send_key(vk, true, down);
    }
    // Lift anything still held so nothing sticks after a handover or drop.
    void deactivate() {
        keys_.release_all();
        static const DWORD ups[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};
        for (int b = 0; b < 3; ++b)
            if (buttons_ & (1u << b)) { INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = ups[b]; SendInput(1, &in, sizeof(in)); }
        buttons_ = 0;
        if (active_) cursor_.hide();
        active_ = false;
    }
    void poll_cursor() { cursor_.poll(); }
    void restore_cursor() { cursor_.show(); }

private:
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
    KeyMapper keys_;
    CursorHider cursor_;
    int left_ = 0, top_ = 0, width_ = 1, height_ = 1;
    double x_ = 0, y_ = 0;
    bool active_ = false, from_right_ = false;
    unsigned buttons_ = 0;
};

// ---- Clipboard -------------------------------------------------------------------

struct Clip {
    uint8_t type = 0;
    Bytes data;
    bool operator==(const Clip& o) const { return type == o.type && data == o.data; }
};

Bytes dib_to_bmp(const uint8_t* dib, size_t size) {
    if (size < sizeof(BITMAPINFOHEADER)) return {};
    const auto* h = reinterpret_cast<const BITMAPINFOHEADER*>(dib);
    DWORD colors = h->biClrUsed;
    if (!colors && h->biBitCount <= 8) colors = 1u << h->biBitCount;
    DWORD offset = sizeof(BITMAPFILEHEADER) + h->biSize + colors * 4;
    if (h->biCompression == BI_BITFIELDS && h->biSize == sizeof(BITMAPINFOHEADER)) offset += 12;
    BITMAPFILEHEADER f{};
    f.bfType = 0x4D42;
    f.bfSize = DWORD(sizeof(f) + size);
    f.bfOffBits = offset;
    Bytes out;
    append(out, &f, sizeof(f));
    append(out, dib, size);
    return out;
}

// PNG -> 32-bit bottom-up DIB through WIC, for apps that do not read "PNG".
Bytes png_to_dib(const Bytes& png) {
    Bytes out;
    IWICImagingFactory* factory = nullptr;
    IStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, png.size());
    if (mem) { std::memcpy(GlobalLock(mem), png.data(), png.size()); GlobalUnlock(mem); }
    UINT w = 0, h = 0;
    if (mem && SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &stream)) &&
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&w, &h)) && w && h && uint64_t(w) * h * 4 < kMaxClip * 4) {
        const UINT stride = w * 4;
        Bytes pixels(size_t(stride) * h);
        if (SUCCEEDED(converter->CopyPixels(nullptr, stride, UINT(pixels.size()), pixels.data()))) {
            BITMAPINFOHEADER bi{};
            bi.biSize = sizeof(bi); bi.biWidth = LONG(w); bi.biHeight = LONG(h);
            bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB; bi.biSizeImage = DWORD(pixels.size());
            append(out, &bi, sizeof(bi));
            for (UINT row = h; row-- > 0;) append(out, pixels.data() + size_t(row) * stride, stride);
        }
    } else if (mem && !stream) {
        GlobalFree(mem);
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (stream) stream->Release();
    return out;
}

} // namespace

// ---- Shared state ------------------------------------------------------------------

struct EdgeControl::Impl {
    struct Paired {
        std::string name;
        Key key{};
        bool control = true;
        bool clipboard = true;
    };
    struct Session {
        SOCKET sock = INVALID_SOCKET;
        std::string id;
        std::mutex send_mu;
        Cipher cipher;
        uint64_t send_counter = 0;
    };

    std::filesystem::path dir;
    uint16_t listen_port = 7021;
    std::array<uint8_t, 16> pc_id{};

    mutable std::mutex mu;
    Config cfg;
    std::map<std::string, Paired> devices;
    std::vector<std::shared_ptr<Session>> sessions;
    std::string receiver_name;
    bool listening = false;
    std::string problem;

    std::mutex input_mu;
    Injector input;
    std::shared_ptr<Session> controller;

    std::atomic<bool> stopping{false};
    std::atomic<bool> pairing_busy{false};
    SOCKET listener = INVALID_SOCKET;
    std::thread accept_thread, cursor_thread, clip_thread;
    std::atomic<int> workers{0};

    // Clipboard thread state.
    std::atomic<HWND> clip_window{nullptr};
    std::mutex clip_mu;
    std::deque<Clip> clip_incoming;
    Clip last_clip;
    DWORD ignore_sequence = 0;

    // ---- Persistence ----
    std::filesystem::path config_path() const { return dir / L"edge-control.ini"; }
    std::filesystem::path devices_path() const { return dir / L"edge-devices.dat"; }

    void load() {
        cfg = Config{};
        cfg.keys.rules = default_rules();
        bool have_id = false, custom_rules = false;
        std::vector<KeyRule> rules;
        std::ifstream in(config_path());
        std::string line;
        while (std::getline(in, line)) {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "enabled") cfg.enabled = v == "1";
            else if (k == "clipboard") cfg.clipboard = v == "1";
            else if (k == "pc_id") have_id = unhex(v, pc_id.data(), pc_id.size());
            else if (k == "command" || k == "option" || k == "control") {
                const int n = std::atoi(v.c_str());
                if (n < 0 || n > 2) continue;
                (k == "command" ? cfg.keys.command : k == "option" ? cfg.keys.option : cfg.keys.control) = ModTarget(n);
            }
            else if (k == "rules") custom_rules = true;
            else if (k == "rule") { KeyRule r; if (rule_from_text(v, r)) rules.push_back(r); }
        }
        if (custom_rules) cfg.keys.rules = rules;
        if (!have_id) { random_bytes(pc_id.data(), pc_id.size()); save_config(); }

        std::ifstream din(devices_path(), std::ios::binary);
        const Bytes blob((std::istreambuf_iterator<char>(din)), {});
        std::istringstream lines(blob.empty() ? std::string() : unprotect(blob));
        while (std::getline(lines, line)) {
            // id \t control \t clipboard \t key \t name
            std::vector<std::string> f;
            size_t start = 0;
            for (int i = 0; i < 4; ++i) {
                const auto tab = line.find('\t', start);
                if (tab == std::string::npos) break;
                f.push_back(line.substr(start, tab - start));
                start = tab + 1;
            }
            if (f.size() != 4) continue;
            Paired p;
            p.control = f[1] == "1";
            p.clipboard = f[2] == "1";
            p.name = line.substr(start);
            uint8_t id[16];
            if (unhex(f[0], id, 16) && unhex(f[3], p.key.data(), 32)) devices[f[0]] = p;
        }
    }

    void save_config() const {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream out(config_path(), std::ios::trunc);
        out << "enabled=" << (cfg.enabled ? 1 : 0) << '\n'
            << "clipboard=" << (cfg.clipboard ? 1 : 0) << '\n'
            << "pc_id=" << hex(pc_id.data(), pc_id.size()) << '\n'
            << "command=" << int(cfg.keys.command) << '\n'
            << "option=" << int(cfg.keys.option) << '\n'
            << "control=" << int(cfg.keys.control) << '\n';
        if (cfg.keys.rules != default_rules()) {
            out << "rules=custom\n";
            for (const auto& r : cfg.keys.rules) out << "rule=" << rule_to_text(r) << '\n';
        }
    }

    void save_devices() const {
        std::string text;
        for (const auto& [id, p] : devices)
            text += id + '\t' + (p.control ? "1" : "0") + '\t' + (p.clipboard ? "1" : "0") + '\t' +
                    hex(p.key.data(), 32) + '\t' + p.name + '\n';
        const Bytes blob = protect(text);
        SecureZeroMemory(text.data(), text.size());
        if (blob.empty()) { LOG_WARN << "edge: could not protect the device keys"; return; }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto tmp = std::filesystem::path(devices_path()).concat(L".tmp");
        { std::ofstream out(tmp, std::ios::binary | std::ios::trunc); out.write((const char*)blob.data(), std::streamsize(blob.size())); }
        MoveFileExW(tmp.c_str(), devices_path().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    // ---- Sessions ----
    bool send(Session& s, const Bytes& plain) {
        std::lock_guard<std::mutex> lock(s.send_mu);
        uint8_t nonce[12] = {1};
        const uint64_t c = ++s.send_counter;
        for (int i = 0; i < 8; ++i) nonce[4 + i] = uint8_t(c >> (8 * i));
        Bytes sealed;
        if (!s.cipher.seal(nonce, plain, sealed)) return false;
        const uint32_t len = uint32_t(12 + sealed.size());
        const uint8_t head[4] = {uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)};
        return send_all(s.sock, head, 4) && send_all(s.sock, nonce, 12) && send_all(s.sock, sealed.data(), sealed.size());
    }

    Bytes config_message(const std::string& id) {
        std::lock_guard<std::mutex> lock(mu);
        auto it = devices.find(id);
        const bool clip = cfg.clipboard && it != devices.end() && it->second.clipboard;
        Bytes m{kConfigMsg, uint8_t(clip ? 1 : 0)};
        append(m, receiver_name.data(), std::min<size_t>(receiver_name.size(), 200));
        return m;
    }

    void push_config() {
        std::vector<std::shared_ptr<Session>> all;
        { std::lock_guard<std::mutex> lock(mu); all = sessions; }
        for (auto& s : all) send(*s, config_message(s->id));
    }

    bool clipboard_allowed(const std::string& id) {
        std::lock_guard<std::mutex> lock(mu);
        auto it = devices.find(id);
        return cfg.enabled && cfg.clipboard && it != devices.end() && it->second.clipboard;
    }

    bool control_allowed(const std::string& id) {
        std::lock_guard<std::mutex> lock(mu);
        auto it = devices.find(id);
        return cfg.enabled && it != devices.end() && it->second.control;
    }

    void release_control(const std::shared_ptr<Session>& s) {
        std::lock_guard<std::mutex> lock(input_mu);
        if (controller == s) { input.deactivate(); controller.reset(); }
    }

    void session(SOCKET sock) {
        uint8_t hello[32];
        if (!recv_all(sock, hello, 32)) return;
        const std::string id = hex(hello, 16);
        Key psk{};
        bool known = false, allowed = false;
        {
            std::lock_guard<std::mutex> lock(mu);
            auto it = devices.find(id);
            known = it != devices.end();
            if (known) psk = it->second.key;
            allowed = cfg.enabled;
        }
        if (!known || !allowed) {
            const uint8_t refuse[6] = {'F', 'E', 'D', 'G', 'X', uint8_t(known ? 2 : 1)};
            send_all(sock, refuse, 6);
            LOG_INFO << "edge: session refused (" << (known ? "turned off" : "unknown Mac") << ")";
            return;
        }
        uint8_t reply[21] = {'F', 'E', 'D', 'G', '2'};
        random_bytes(reply + 5, 16);
        if (!send_all(sock, reply, 21)) return;
        Bytes material(psk.begin(), psk.end());
        append(material, hello + 16, 16);
        append(material, reply + 5, 16);
        auto s = std::make_shared<Session>();
        s->sock = sock;
        s->id = id;
        if (!s->cipher.init(sha256(material))) return;
        set_timeout(sock, 15000);  // the Mac pings every 2 s

        uint64_t recv_counter = 0;
        bool authenticated = false;
        Bytes frame, plain;
        std::string clip_buffer;
        for (;;) {
            uint8_t head[4];
            if (!recv_all(sock, head, 4)) break;
            const uint32_t len = uint32_t(head[0]) << 24 | uint32_t(head[1]) << 16 | uint32_t(head[2]) << 8 | head[3];
            if (len < 12 + 16 + 1 || len > kMaxFrame) break;
            frame.resize(len);
            if (!recv_all(sock, frame.data(), len)) break;
            uint64_t counter = 0;
            for (int i = 7; i >= 0; --i) counter = counter << 8 | frame[4 + i];
            if (frame[0] != 0 || counter <= recv_counter || !s->cipher.open(frame.data(), frame.data() + 12, len - 12, plain) || plain.empty()) {
                LOG_WARN << "edge: rejected a frame";
                break;
            }
            recv_counter = counter;
            const uint8_t* p = plain.data() + 1;
            const size_t n = plain.size() - 1;
            if (!authenticated) {
                if (plain[0] != kHello) break;
                authenticated = true;
                { std::lock_guard<std::mutex> lock(mu); sessions.push_back(s); }
                LOG_INFO << "edge: Mac connected";
                send(*s, config_message(id));
                continue;
            }
            switch (plain[0]) {
                case kEnter: {
                    if (n < 3) break;
                    const double yfrac = uint16_t(p[0] << 8 | p[1]) / 65535.0;
                    bool ok = control_allowed(id) && input_desktop_usable();
                    {
                        std::lock_guard<std::mutex> lock(input_mu);
                        if (ok) {
                            if (controller && controller != s) input.deactivate();
                            controller = s;
                            input.enter(yfrac, p[2] != 0, n >= 4 && p[3] != 0);
                        }
                    }
                    if (!ok) {
                        // Hand the pointer straight back rather than strand it.
                        send(*s, {kLeave, p[0], p[1]});
                        LOG_INFO << "edge: entry refused (control off, or the PC is locked)";
                    }
                    break;
                }
                case kMove: {
                    if (n < 4) break;
                    double yfrac = 0;
                    bool left = false;
                    {
                        std::lock_guard<std::mutex> lock(input_mu);
                        if (controller == s)
                            left = input.move(int16_t(p[0] << 8 | p[1]), int16_t(p[2] << 8 | p[3]), yfrac);
                        if (left) controller.reset();
                    }
                    if (left) {
                        const uint16_t y = uint16_t(std::clamp(yfrac, 0.0, 1.0) * 65535);
                        send(*s, {kLeave, uint8_t(y >> 8), uint8_t(y)});
                    }
                    break;
                }
                case kButton: if (n >= 2) { std::lock_guard<std::mutex> lock(input_mu); if (controller == s) input.button(p[0], p[1] != 0); } break;
                case kScroll: if (n >= 4) { std::lock_guard<std::mutex> lock(input_mu); if (controller == s) input.scroll(int16_t(p[0] << 8 | p[1]), int16_t(p[2] << 8 | p[3])); } break;
                case kKey: if (n >= 3) { std::lock_guard<std::mutex> lock(input_mu); if (controller == s) input.key(uint16_t(p[0] << 8 | p[1]), p[2] != 0); } break;
                case kMedia: if (n >= 2) { std::lock_guard<std::mutex> lock(input_mu); if (controller == s) input.media(p[0], p[1] != 0); } break;
                case kText: { std::lock_guard<std::mutex> lock(input_mu); if (controller == s) input.text(std::string((const char*)p, n)); } break;
                case kPing: send(*s, {kPing}); break;   // lets the Mac notice a vanished PC quickly
                case kRelease: release_control(s); break;
                case kClip:
                    if (n >= 1 && n - 1 <= kMaxClip && clipboard_allowed(id)) {
                        Clip c{p[0], Bytes(p + 1, p + n)};
                        {
                            std::lock_guard<std::mutex> lock(clip_mu);
                            clip_incoming.push_back(std::move(c));
                        }
                        if (HWND w = clip_window.load()) PostMessageW(w, WM_APP + 1, 0, 0);
                    }
                    break;
                default: break;
            }
        }
        release_control(s);
        if (authenticated) {
            std::lock_guard<std::mutex> lock(mu);
            sessions.erase(std::remove(sessions.begin(), sessions.end(), s), sessions.end());
            LOG_INFO << "edge: Mac disconnected";
        }
    }

    // ---- Pairing ----
    void pair(SOCKET sock) {
        set_timeout(sock, 180000);
        uint8_t first[49];
        if (!recv_all(sock, first, 49)) return;
        const uint8_t* mac_pub = first;
        const std::string mac_id = hex(first + 32, 16);
        std::string mac_name(first[48], '\0');
        if (first[48] && !recv_all(sock, mac_name.data(), mac_name.size())) return;
        if (mac_name.empty()) mac_name = "Mac";
        bool enabled;
        { std::lock_guard<std::mutex> lock(mu); enabled = cfg.enabled; }
        if (!enabled || pairing_busy.exchange(true)) { LOG_INFO << "edge: pairing request declined (off or busy)"; return; }
        struct Done { std::atomic<bool>& f; ~Done() { f = false; } } done{pairing_busy};

        uint8_t pc_pub[32], shared[32], pc_nonce[16], mac_nonce[16];
        if (!x25519(mac_pub, pc_pub, shared)) return;
        random_bytes(pc_nonce, 16);
        Bytes commit_in;
        append(commit_in, "fucair-commit");
        append(commit_in, pc_pub, 32); append(commit_in, pc_nonce, 16); append(commit_in, mac_pub, 32);
        const Key commit = sha256(commit_in);
        char host[256] = {};
        DWORD host_len = sizeof(host);
        GetComputerNameExA(ComputerNamePhysicalDnsHostname, host, &host_len);
        std::string pc_name;
        { std::lock_guard<std::mutex> lock(mu); pc_name = receiver_name.empty() ? host : receiver_name; }
        pc_name.resize(std::min<size_t>(pc_name.size(), 200));
        Bytes reply;
        append(reply, "FPAR2");
        append(reply, pc_pub, 32); append(reply, pc_id.data(), 16); append(reply, commit.data(), 32);
        reply.push_back(uint8_t(pc_name.size())); append(reply, pc_name.data(), pc_name.size());
        if (!send_all(sock, reply.data(), reply.size()) || !recv_all(sock, mac_nonce, 16) || !send_all(sock, pc_nonce, 16)) return;

        Bytes code_in;
        append(code_in, "fucair-code");
        append(code_in, mac_pub, 32); append(code_in, pc_pub, 32); append(code_in, mac_nonce, 16); append(code_in, pc_nonce, 16);
        const Key code_hash = sha256(code_in);
        const uint32_t code = (uint32_t(code_hash[0]) << 24 | uint32_t(code_hash[1]) << 16 | uint32_t(code_hash[2]) << 8 | code_hash[3]) % 1000000;
        wchar_t code_text[16];
        swprintf(code_text, 16, L"%03u %03u", code / 1000, code % 1000);
        LOG_INFO << "edge: pairing request from a Mac; waiting for confirmation";

        char body[600];
        std::snprintf(body, sizeof(body), tr("\"%s\" wants to use its keyboard and mouse with this PC.\n\nPair only if the Mac shows the same code:"), mac_name.c_str());
        const std::wstring text = widen(body) + L"\n\n            " + code_text + L"\n\n" + widen(tr("Pairing is remembered. You can remove the Mac in Fucairserver at any time."));
        const int answer = MessageBoxW(nullptr, text.c_str(), widen(tr("Pair a Mac with Fucairserver")).c_str(),
                                       MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND | MB_DEFBUTTON2);
        const uint8_t ours = answer == IDYES ? 1 : 0;
        uint8_t theirs = 0;
        if (!send_all(sock, &ours, 1) || !recv_all(sock, &theirs, 1) || !ours || !theirs) {
            LOG_INFO << "edge: pairing not confirmed";
            return;
        }
        Bytes key_in;
        append(key_in, "fucair-edge-pair-v2");
        append(key_in, shared, 32); append(key_in, mac_pub, 32); append(key_in, pc_pub, 32);
        append(key_in, mac_nonce, 16); append(key_in, pc_nonce, 16);
        const Key key = sha256(key_in);
        SecureZeroMemory(shared, sizeof(shared));
        uint8_t got[32];
        const Key expect = hmac(key, "mac-confirm"), confirm = hmac(key, "pc-confirm");
        if (!recv_all(sock, got, 32) || std::memcmp(got, expect.data(), 32) != 0) { LOG_WARN << "edge: pairing key confirmation failed"; return; }
        if (!send_all(sock, confirm.data(), 32)) return;
        {
            std::lock_guard<std::mutex> lock(mu);
            Paired& p = devices[mac_id];
            p.name = mac_name;
            p.key = key;
            save_devices();
        }
        LOG_INFO << "edge: paired with a Mac";
    }

    void handle(SOCKET sock) {
        BOOL nodelay = TRUE;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        set_timeout(sock, 10000);
        uint8_t magic[5];
        if (recv_all(sock, magic, 5)) {
            if (std::memcmp(magic, "FPAR2", 5) == 0) pair(sock);
            else if (std::memcmp(magic, "FEDG2", 5) == 0) session(sock);
        }
        shutdown(sock, SD_BOTH);
        closesocket(sock);
    }

    void accept_loop() {
        while (!stopping) {
            fd_set set; FD_ZERO(&set); FD_SET(listener, &set);
            timeval tv{0, 300000};
            if (select(0, &set, nullptr, nullptr, &tv) <= 0) continue;
            SOCKET s = accept(listener, nullptr, nullptr);
            if (s == INVALID_SOCKET) continue;
            ++workers;
            std::thread([this, s] { handle(s); --workers; }).detach();
        }
    }

    // ---- Clipboard ----
    static LRESULT CALLBACK clip_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && msg == WM_CLIPBOARDUPDATE) { self->clipboard_changed(hwnd); return 0; }
        if (self && msg == WM_APP + 1) { self->apply_incoming(hwnd); return 0; }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    static bool open_clipboard(HWND hwnd) {
        for (int i = 0; i < 10; ++i) { if (OpenClipboard(hwnd)) return true; Sleep(20); }
        return false;
    }

    void clipboard_changed(HWND hwnd) {
        if (GetClipboardSequenceNumber() == ignore_sequence) return;
        std::vector<std::shared_ptr<Session>> targets;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!cfg.enabled || !cfg.clipboard) return;
            for (auto& s : sessions) {
                auto it = devices.find(s->id);
                if (it != devices.end() && it->second.clipboard) targets.push_back(s);
            }
        }
        if (targets.empty()) return;
        static const UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        static const UINT png_format = RegisterClipboardFormatW(L"PNG");
        if (IsClipboardFormatAvailable(exclude)) return;  // password managers
        if (!open_clipboard(hwnd)) return;
        Clip c;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
                c.type = kClipText;
                const std::string s = narrow(w, wcsnlen(w, GlobalSize(h) / sizeof(wchar_t)));
                c.data.assign(s.begin(), s.end());
                GlobalUnlock(h);
            }
        } else if (HANDLE h = GetClipboardData(png_format)) {
            if (auto* p = static_cast<const uint8_t*>(GlobalLock(h))) { c.type = kClipPng; c.data.assign(p, p + GlobalSize(h)); GlobalUnlock(h); }
        } else if (HANDLE h = GetClipboardData(CF_DIB)) {
            if (auto* p = static_cast<const uint8_t*>(GlobalLock(h))) { c.type = kClipBmp; c.data = dib_to_bmp(p, GlobalSize(h)); GlobalUnlock(h); }
        }
        CloseClipboard();
        if (!c.type || c.data.empty() || c.data.size() > kMaxClip || c == last_clip) return;
        last_clip = c;
        Bytes m{kClip, c.type};
        append(m, c.data.data(), c.data.size());
        for (auto& s : targets) send(*s, m);
    }

    void apply_incoming(HWND hwnd) {
        std::deque<Clip> pending;
        { std::lock_guard<std::mutex> lock(clip_mu); pending.swap(clip_incoming); }
        if (pending.empty()) return;
        Clip c = std::move(pending.back());
        if (c == last_clip) return;
        auto global = [](const void* p, size_t n) {
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
            if (g) { std::memcpy(GlobalLock(g), p, n); GlobalUnlock(g); }
            return g;
        };
        static const UINT png_format = RegisterClipboardFormatW(L"PNG");
        Bytes dib;
        if (c.type == kClipPng) dib = png_to_dib(c.data);
        if (!open_clipboard(hwnd)) return;
        EmptyClipboard();
        if (c.type == kClipText) {
            std::wstring w = widen(std::string(c.data.begin(), c.data.end()));
            if (HGLOBAL g = global(w.c_str(), (w.size() + 1) * sizeof(wchar_t))) SetClipboardData(CF_UNICODETEXT, g);
        } else if (c.type == kClipPng) {
            if (HGLOBAL g = global(c.data.data(), c.data.size())) SetClipboardData(png_format, g);
            if (!dib.empty()) if (HGLOBAL g = global(dib.data(), dib.size())) SetClipboardData(CF_DIB, g);
        } else if (c.type == kClipBmp && c.data.size() > sizeof(BITMAPFILEHEADER)) {
            if (HGLOBAL g = global(c.data.data() + sizeof(BITMAPFILEHEADER), c.data.size() - sizeof(BITMAPFILEHEADER))) SetClipboardData(CF_DIB, g);
        }
        CloseClipboard();
        ignore_sequence = GetClipboardSequenceNumber();
        last_clip = std::move(c);
    }

    void clip_loop() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        WNDCLASSW wc{};
        wc.lpfnWndProc = clip_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FucairserverEdgeClipboard";
        RegisterClassW(&wc);
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!hwnd) { CoUninitialize(); return; }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        AddClipboardFormatListener(hwnd);
        clip_window = hwnd;
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
        clip_window = nullptr;
        RemoveClipboardFormatListener(hwnd);
        DestroyWindow(hwnd);
        CoUninitialize();
    }
};

// ---- Public interface ------------------------------------------------------------

EdgeControl::EdgeControl() : impl_(std::make_unique<Impl>()) {}
EdgeControl::~EdgeControl() { stop(); }

bool EdgeControl::start(const std::filesystem::path& dir, uint16_t port) {
    auto& m = *impl_;
    m.dir = dir;
    m.listen_port = port;
    m.stopping = false;
    {
        std::lock_guard<std::mutex> lock(m.mu);
        m.load();
    }
    m.input.set_keys(m.cfg.keys);
    m.input.restore_cursor();

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    m.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    BOOL exclusive = TRUE;
    setsockopt(m.listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
    const bool ok = m.listener != INVALID_SOCKET &&
                    bind(m.listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 && listen(m.listener, 4) == 0;
    {
        std::lock_guard<std::mutex> lock(m.mu);
        m.listening = ok;
        m.problem = ok ? "" : "Another program is using the edge control port.";
    }
    if (!ok) {
        LOG_WARN << "edge: cannot listen on port " << port;
        if (m.listener != INVALID_SOCKET) closesocket(m.listener);
        m.listener = INVALID_SOCKET;
        return false;
    }
    LOG_INFO << "edge: listening on port " << port;
    m.accept_thread = std::thread([&m] { m.accept_loop(); });
    m.clip_thread = std::thread([&m] { m.clip_loop(); });
    m.cursor_thread = std::thread([&m] {
        while (!m.stopping) {
            m.input.poll_cursor();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    return true;
}

void EdgeControl::stop() {
    auto& m = *impl_;
    if (m.stopping.exchange(true)) return;
    if (m.listener != INVALID_SOCKET) { closesocket(m.listener); m.listener = INVALID_SOCKET; }
    {
        std::lock_guard<std::mutex> lock(m.mu);
        for (auto& s : m.sessions) shutdown(s->sock, SD_BOTH);
    }
    if (m.accept_thread.joinable()) m.accept_thread.join();
    if (HWND w = m.clip_window.load()) PostMessageW(w, WM_QUIT, 0, 0);
    if (m.clip_thread.joinable()) m.clip_thread.join();
    if (m.cursor_thread.joinable()) m.cursor_thread.join();
    // Connection threads end once their sockets close; a pairing dialog that
    // is still open ends with the process.
    for (int i = 0; i < 40 && m.workers > (m.pairing_busy ? 1 : 0); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::lock_guard<std::mutex> lock(m.mu);
        m.listening = false;
    }
    {
        std::lock_guard<std::mutex> lock(m.input_mu);
        m.input.deactivate();
        m.controller.reset();
    }
    m.input.restore_cursor();
}

Config EdgeControl::config() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->cfg;
}

void EdgeControl::set_config(const Config& config) {
    auto& m = *impl_;
    bool turned_off = false, keys_changed = false;
    {
        std::lock_guard<std::mutex> lock(m.mu);
        if (m.cfg == config) return;
        turned_off = m.cfg.enabled && !config.enabled;
        keys_changed = !(m.cfg.keys == config.keys);
        m.cfg = config;
        m.save_config();
    }
    {
        std::lock_guard<std::mutex> lock(m.input_mu);
        if (turned_off && m.controller) {
            m.input.deactivate();
            m.send(*m.controller, {kLeave, 0x80, 0x00});
            m.controller.reset();
        }
        if (keys_changed) m.input.set_keys(config.keys);
    }
    m.push_config();
}

Status EdgeControl::status() const {
    auto& m = *impl_;
    Status st;
    std::shared_ptr<Impl::Session> controller;
    {
        std::lock_guard<std::mutex> lock(m.input_mu);
        controller = m.controller;
    }
    std::lock_guard<std::mutex> lock(m.mu);
    st.listening = m.listening;
    st.problem = m.problem;
    for (const auto& [id, p] : m.devices) {
        Device d{id, p.name, p.control, p.clipboard, false, false};
        for (const auto& s : m.sessions) if (s->id == id) d.online = true;
        d.controlling = controller && controller->id == id;
        st.devices.push_back(d);
    }
    return st;
}

void EdgeControl::set_device(const std::string& id, bool control, bool clipboard) {
    auto& m = *impl_;
    {
        std::lock_guard<std::mutex> lock(m.mu);
        auto it = m.devices.find(id);
        if (it == m.devices.end() || (it->second.control == control && it->second.clipboard == clipboard)) return;
        it->second.control = control;
        it->second.clipboard = clipboard;
        m.save_devices();
    }
    if (!control) {
        std::lock_guard<std::mutex> lock(m.input_mu);
        if (m.controller && m.controller->id == id) {
            m.input.deactivate();
            m.send(*m.controller, {kLeave, 0x80, 0x00});
            m.controller.reset();
        }
    }
    m.push_config();
}

void EdgeControl::remove_device(const std::string& id) {
    auto& m = *impl_;
    std::lock_guard<std::mutex> lock(m.mu);
    if (!m.devices.erase(id)) return;
    m.save_devices();
    for (auto& s : m.sessions) if (s->id == id) shutdown(s->sock, SD_BOTH);
}

std::string EdgeControl::pc_id() const { return hex(impl_->pc_id.data(), impl_->pc_id.size()); }
uint16_t EdgeControl::port() const { return impl_->listen_port; }

void EdgeControl::set_receiver_name(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (impl_->receiver_name == name) return;
        impl_->receiver_name = name;
    }
    impl_->push_config();
}

} // namespace fucairserver::edge
