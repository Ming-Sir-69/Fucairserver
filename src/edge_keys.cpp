#include "edge_keys.h"

#include <algorithm>
#include <cstdio>

namespace fucairserver::edge {
namespace {

enum : uint16_t {
    kMacCommandL = 55, kMacCommandR = 54, kMacOptionL = 58, kMacOptionR = 61,
    kMacControlL = 59, kMacControlR = 62, kMacShiftL = 56, kMacShiftR = 60,
    kMacCapsLock = 57,
};
constexpr uint16_t kMaskVk = 0xE8;  // unassigned virtual key

enum : uint16_t {
    VK_LSHIFT_ = 0xA0, VK_RSHIFT_ = 0xA1, VK_LCTRL_ = 0xA2, VK_RCTRL_ = 0xA3,
    VK_LALT_ = 0xA4, VK_RALT_ = 0xA5, VK_LWIN_ = 0x5B, VK_RWIN_ = 0x5C,
};

uint8_t category(uint16_t mac) {
    switch (mac) {
        case kMacCommandL: case kMacCommandR: return kCommand;
        case kMacOptionL: case kMacOptionR: return kOption;
        case kMacControlL: case kMacControlR: return kControl;
        case kMacShiftL: case kMacShiftR: return kShift;
        default: return 0;
    }
}

bool is_right(uint16_t mac) { return mac == kMacCommandR || mac == kMacOptionR || mac == kMacControlR || mac == kMacShiftR; }

const std::vector<KeyName> kKeys = {
    {"A", 0, 'A', false}, {"B", 11, 'B', false}, {"C", 8, 'C', false}, {"D", 2, 'D', false},
    {"E", 14, 'E', false}, {"F", 3, 'F', false}, {"G", 5, 'G', false}, {"H", 4, 'H', false},
    {"I", 34, 'I', false}, {"J", 38, 'J', false}, {"K", 40, 'K', false}, {"L", 37, 'L', false},
    {"M", 46, 'M', false}, {"N", 45, 'N', false}, {"O", 31, 'O', false}, {"P", 35, 'P', false},
    {"Q", 12, 'Q', false}, {"R", 15, 'R', false}, {"S", 1, 'S', false}, {"T", 17, 'T', false},
    {"U", 32, 'U', false}, {"V", 9, 'V', false}, {"W", 13, 'W', false}, {"X", 7, 'X', false},
    {"Y", 16, 'Y', false}, {"Z", 6, 'Z', false},
    {"0", 29, '0', false}, {"1", 18, '1', false}, {"2", 19, '2', false}, {"3", 20, '3', false},
    {"4", 21, '4', false}, {"5", 23, '5', false}, {"6", 22, '6', false}, {"7", 26, '7', false},
    {"8", 28, '8', false}, {"9", 25, '9', false},
    {"Space", 49, 0x20, false}, {"Tab", 48, 0x09, false}, {"Return", 36, 0x0D, false},
    {"Esc", 53, 0x1B, false}, {"Backspace", 51, 0x08, false}, {"Delete", 117, 0x2E, true},
    {"Left", 123, 0x25, true}, {"Right", 124, 0x27, true}, {"Up", 126, 0x26, true}, {"Down", 125, 0x28, true},
    {"Home", 115, 0x24, true}, {"End", 119, 0x23, true}, {"Page Up", 116, 0x21, true}, {"Page Down", 121, 0x22, true},
    {"Insert", 114, 0x2D, true},
    {"F1", 122, 0x70, false}, {"F2", 120, 0x71, false}, {"F3", 99, 0x72, false}, {"F4", 118, 0x73, false},
    {"F5", 96, 0x74, false}, {"F6", 97, 0x75, false}, {"F7", 98, 0x76, false}, {"F8", 100, 0x77, false},
    {"F9", 101, 0x78, false}, {"F10", 109, 0x79, false}, {"F11", 103, 0x7A, false}, {"F12", 111, 0x7B, false},
    {"F13", 105, 0x7C, false}, {"F14", 107, 0x7D, false}, {"F15", 113, 0x7E, false}, {"F16", 106, 0x7F, false},
    {"F17", 64, 0x80, false}, {"F18", 79, 0x81, false}, {"F19", 80, 0x82, false}, {"F20", 90, 0x83, false},
    {"-", 27, 0xBD, false}, {"=", 24, 0xBB, false}, {"[", 33, 0xDB, false}, {"]", 30, 0xDD, false},
    {";", 41, 0xBA, false}, {"'", 39, 0xDE, false}, {"\\", 42, 0xDC, false}, {",", 43, 0xBC, false},
    {".", 47, 0xBE, false}, {"/", 44, 0xBF, false}, {"`", 50, 0xC0, false}, {"ISO section key", 10, 0xE2, false},
    {"Keypad 0", 82, 0x60, false}, {"Keypad 1", 83, 0x61, false}, {"Keypad 2", 84, 0x62, false},
    {"Keypad 3", 85, 0x63, false}, {"Keypad 4", 86, 0x64, false}, {"Keypad 5", 87, 0x65, false},
    {"Keypad 6", 88, 0x66, false}, {"Keypad 7", 89, 0x67, false}, {"Keypad 8", 91, 0x68, false},
    {"Keypad 9", 92, 0x69, false}, {"Keypad *", 67, 0x6A, false}, {"Keypad +", 69, 0x6B, false},
    {"Keypad -", 78, 0x6D, false}, {"Keypad .", 65, 0x6E, false}, {"Keypad /", 75, 0x6F, true},
    {"Keypad Enter", 76, 0x0D, true}, {"Caps Lock", 57, 0x14, false},
    {"fn (Globe)", kMacFn, 0, false},
    {"Print Screen", kNoMac, 0x2C, true}, {"Pause", kNoMac, 0x13, false}, {"Menu", kNoMac, 0x5D, true},
    {"Volume Mute", kNoMac, 0xAD, true}, {"Volume Down", kNoMac, 0xAE, true}, {"Volume Up", kNoMac, 0xAF, true},
    {"Next Track", kNoMac, 0xB0, true}, {"Previous Track", kNoMac, 0xB1, true}, {"Play/Pause", kNoMac, 0xB3, true},
    {"F21", kNoMac, 0x84, false}, {"F22", kNoMac, 0x85, false}, {"F23", kNoMac, 0x86, false}, {"F24", kNoMac, 0x87, false},
};

} // namespace

const std::vector<KeyName>& key_names() { return kKeys; }

const KeyName* key_by_mac(uint16_t mac) {
    if (mac == kNoMac) return nullptr;
    for (const auto& k : kKeys) if (k.mac == mac) return &k;
    return nullptr;
}

const KeyName* key_by_vk(uint16_t vk) {
    if (vk == 0) return nullptr;
    for (const auto& k : kKeys) if (k.vk == vk) return &k;
    return nullptr;
}

std::vector<KeyRule> default_rules() {
    return {
        // App switching and closing, the Mac way.
        {true, kCommand, 48, kAlt, 0x09},             // Command-Tab -> Alt-Tab
        {true, kCommand, 12, kAlt, 0x73},             // Command-Q -> Alt-F4
        // Text navigation, the Mac way.
        {true, kCommand, 123, 0, 0x24},               // Command-Left -> Home
        {true, kCommand, 124, 0, 0x23},               // Command-Right -> End
        {true, kCommand, 126, kCtrl, 0x24},           // Command-Up -> Ctrl-Home
        {true, kCommand, 125, kCtrl, 0x23},           // Command-Down -> Ctrl-End
        {true, kOption, 123, kCtrl, 0x25},            // Option-Left -> Ctrl-Left
        {true, kOption, 124, kCtrl, 0x27},            // Option-Right -> Ctrl-Right
        {true, kOption, 51, kCtrl, 0x08},             // Option-Backspace -> Ctrl-Backspace
    };
}

std::string rule_to_text(const KeyRule& r) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d,%u,%u,%u,%u", r.enabled ? 1 : 0, r.from_mods, r.from_key, r.to_mods, r.to_vk);
    return buf;
}

bool rule_from_text(const std::string& text, KeyRule& r) {
    int enabled = 0;
    unsigned from_mods = 0, from_key = 0, to_mods = 0, to_vk = 0;
    if (std::sscanf(text.c_str(), "%d,%u,%u,%u,%u", &enabled, &from_mods, &from_key, &to_mods, &to_vk) != 5) return false;
    if (from_mods > 15 || from_key > 0xFF || to_mods > 31 || to_vk > 0xFF) return false;
    if (!key_by_mac(static_cast<uint16_t>(from_key))) return false;
    if (to_vk != 0 && !key_by_vk(static_cast<uint16_t>(to_vk))) return false;
    if (to_vk == 0 && to_mods == 0) return false;
    r = {enabled != 0, static_cast<uint8_t>(from_mods), static_cast<uint16_t>(from_key),
         static_cast<uint8_t>(to_mods), static_cast<uint16_t>(to_vk)};
    return true;
}

KeyMapper::Out KeyMapper::modifier_out(uint16_t mac) const {
    const bool right = is_right(mac);
    ModTarget target;
    switch (category(mac)) {
        case kShift: return {right ? VK_RSHIFT_ : VK_LSHIFT_, false};
        case kCommand: target = config_.command; break;
        case kOption: target = config_.option; break;
        default: target = config_.control; break;
    }
    switch (target) {
        case ModTarget::Ctrl: return {right ? VK_RCTRL_ : VK_LCTRL_, right};
        case ModTarget::Alt: return {right ? VK_RALT_ : VK_LALT_, right};
        default: return {right ? VK_RWIN_ : VK_LWIN_, true};
    }
}

uint8_t KeyMapper::held_mods() const {
    uint8_t mods = 0;
    for (uint16_t k : physical_) mods |= category(k);
    return mods;
}

// Exact match on Command / Option / Control. Shift is part of the match only
// when the rule names it; otherwise a held Shift is carried over, so
// Shift-Command-Left selects to the start of the line.
const KeyRule* KeyMapper::match(uint16_t mac, uint8_t mods) const {
    for (const auto& r : config_.rules) {
        if (!r.enabled || r.from_key != mac) continue;
        const uint8_t want = r.from_mods;
        const uint8_t have = (want & kShift) ? mods : static_cast<uint8_t>(mods & ~kShift);
        if (want == have) return &r;
    }
    return nullptr;
}

void KeyMapper::press(Out o) { emit_(o.vk, o.ext, true); down_.insert(o); }
void KeyMapper::lift(Out o) { if (down_.erase(o)) emit_(o.vk, o.ext, false); }

// Releasing Alt or Win with nothing pressed in between opens the menu bar or
// the Start menu. An unassigned key in between keeps it a plain release.
void KeyMapper::lift_masked(Out o) {
    if (!down_.count(o)) return;
    if (o.vk == VK_LALT_ || o.vk == VK_RALT_ || o.vk == VK_LWIN_ || o.vk == VK_RWIN_) {
        emit_(kMaskVk, false, true);
        emit_(kMaskVk, false, false);
    }
    lift(o);
}

void KeyMapper::begin_rule(const KeyRule& rule, uint16_t source) {
    std::vector<Out> wanted;
    if (rule.to_mods & kCtrl) wanted.push_back({VK_LCTRL_, false});
    if (rule.to_mods & kAlt) wanted.push_back({VK_LALT_, false});
    if (rule.to_mods & kRightAlt) wanted.push_back({VK_RALT_, true});
    if (rule.to_mods & kWin) wanted.push_back({VK_LWIN_, true});
    if (rule.to_mods & kWinShift) wanted.push_back({VK_LSHIFT_, false});
    auto same_kind = [](uint16_t a, uint16_t b) {
        if (a == b) return true;
        auto kind = [](uint16_t v) { return v == VK_LCTRL_ || v == VK_RCTRL_ ? 1 : v == VK_LWIN_ || v == VK_RWIN_ ? 2 : v == VK_LSHIFT_ || v == VK_RSHIFT_ ? 3 : 0; };
        return kind(a) != 0 && kind(a) == kind(b);
    };
    auto consumed = [&](uint8_t c) { return c && (rule.from_mods & c) && (c != kShift || (rule.from_mods & kShift)); };
    // Lift the Mac modifiers the rule consumes, unless the target wants the same key.
    std::vector<bool> satisfied(wanted.size(), false);
    for (uint16_t k : physical_) {
        if (!consumed(category(k))) continue;
        auto it = sent_.find(k);
        if (it == sent_.end()) continue;
        bool keep = false;
        for (size_t i = 0; i < wanted.size(); ++i)
            if (!satisfied[i] && same_kind(wanted[i].vk, it->second.vk)) { satisfied[i] = keep = true; break; }
        if (keep) continue;
        lift_masked(it->second);
        sent_.erase(it);
        suppressed_.insert(k);
    }
    // Modifiers held back by a previous rule that this one does not consume.
    for (auto it = suppressed_.begin(); it != suppressed_.end();) {
        if (consumed(category(*it)) || !physical_.count(*it)) { ++it; continue; }
        const Out o = modifier_out(*it);
        press(o);
        sent_[*it] = o;
        it = suppressed_.erase(it);
    }
    rule_mods_.clear();
    for (size_t i = 0; i < wanted.size(); ++i) {
        if (satisfied[i] || down_.count(wanted[i])) continue;
        press(wanted[i]);
        rule_mods_.push_back(wanted[i]);
    }
    active_ = &rule;
    active_source_ = source;
}

// restore=false hands the held-back modifiers to the next rule.
void KeyMapper::end_rule(bool restore) {
    if (!active_) return;
    auto src = sent_.find(active_source_);
    if (src != sent_.end()) { lift(src->second); sent_.erase(src); }
    for (auto it = rule_mods_.rbegin(); it != rule_mods_.rend(); ++it) lift(*it);
    rule_mods_.clear();
    active_ = nullptr;
    if (!restore) return;
    for (uint16_t k : suppressed_) {
        if (!physical_.count(k)) continue;
        const Out o = modifier_out(k);
        press(o);
        sent_[k] = o;
    }
    suppressed_.clear();
}

void KeyMapper::fire_tap(const KeyRule& rule) {
    if (active_) end_rule(false);
    begin_rule(rule, kMacFn);
    if (rule.to_vk) {
        const KeyName* k = key_by_vk(rule.to_vk);
        const Out o{rule.to_vk, k && k->extended};
        press(o);
        lift(o);
    }
    end_rule(true);
}

void KeyMapper::key(uint16_t mac, bool down) {
    const uint8_t cat = category(mac);
    if (cat) {
        if (down) {
            if (!physical_.insert(mac).second) return;
            fn_tap_ = false;
            if (active_ && (active_->from_mods & cat) && cat != kShift) { suppressed_.insert(mac); return; }
            const Out o = modifier_out(mac);
            press(o);
            sent_[mac] = o;
        } else {
            physical_.erase(mac);
            const bool ends = active_ && (active_->from_mods & cat);
            if (suppressed_.erase(mac)) {
                if (ends) end_rule(true);
                return;
            }
            auto it = sent_.find(mac);
            if (it != sent_.end()) { lift(it->second); sent_.erase(it); }
            if (ends) end_rule(true);
        }
        return;
    }

    if (mac == kMacFn) {
        // fn / Globe works as a tap: it also modifies other keys, so a rule
        // fires only when fn is released without another key in between.
        if (down) { physical_.insert(mac); fn_tap_ = true; return; }
        physical_.erase(mac);
        if (!fn_tap_) return;
        fn_tap_ = false;
        if (const KeyRule* r = match(kMacFn, held_mods())) fire_tap(*r);
        return;
    }

    const KeyName* name = key_by_mac(mac);
    if (mac == kMacCapsLock) {
        // The Mac reports each Caps Lock toggle as a single event.
        if (down && name) { emit_(name->vk, name->extended, true); emit_(name->vk, name->extended, false); }
        return;
    }

    if (down) {
        fn_tap_ = false;
        const bool repeat = !physical_.insert(mac).second;
        if (active_ && active_source_ == mac) {
            if (active_->to_vk) {
                const KeyName* k = key_by_vk(active_->to_vk);
                const Out o{active_->to_vk, k && k->extended};
                press(o);
                sent_[mac] = o;
            }
            return;
        }
        if (repeat) {
            auto it = sent_.find(mac);
            if (it != sent_.end()) press(it->second);
            return;
        }
        if (const KeyRule* r = match(mac, held_mods())) {
            if (active_) end_rule(false);
            begin_rule(*r, mac);
            if (r->to_vk) {
                const KeyName* k = key_by_vk(r->to_vk);
                const Out o{r->to_vk, k && k->extended};
                press(o);
                sent_[mac] = o;
            }
            return;
        }
        if (active_) end_rule(true);
        if (!name || !name->vk) return;
        const Out o{name->vk, name->extended};
        press(o);
        sent_[mac] = o;
    } else {
        physical_.erase(mac);
        auto it = sent_.find(mac);
        if (it != sent_.end()) { lift(it->second); sent_.erase(it); }
        // A rule without Command/Option/Control ends with its key.
        if (active_ && active_source_ == mac && !(active_->from_mods & (kCommand | kOption | kControl))) end_rule(true);
    }
}

void KeyMapper::release_all() {
    for (auto it = down_.rbegin(); it != down_.rend(); ++it) emit_(it->vk, it->ext, false);
    down_.clear();
    sent_.clear();
    physical_.clear();
    suppressed_.clear();
    rule_mods_.clear();
    active_ = nullptr;
    fn_tap_ = false;
}

} // namespace fucairserver::edge
