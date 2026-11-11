#pragma once

// Keyboard translation for edge control: raw macOS key codes from the Mac
// become Windows virtual keys. Modifiers follow a configurable mapping and
// shortcut rules turn one Mac combination into a different Windows one.

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fucairserver::edge {

// Modifier sets on the Mac side of a rule.
enum MacMod : uint8_t { kCommand = 1, kOption = 2, kControl = 4, kShift = 8 };
// Modifier sets on the Windows side of a rule.
enum WinMod : uint8_t { kCtrl = 1, kAlt = 2, kWin = 4, kWinShift = 8, kRightAlt = 16 };

// What a Mac modifier key acts as on Windows.
enum class ModTarget : uint8_t { Ctrl = 0, Alt = 1, Win = 2 };

constexpr uint16_t kMacFn = 63;  // fn / Globe; only usable as a rule source

struct KeyRule {
    bool enabled = true;
    uint8_t from_mods = 0;   // MacMod bits
    uint16_t from_key = 0;   // macOS key code
    uint8_t to_mods = 0;     // WinMod bits
    uint16_t to_vk = 0;      // Windows virtual key; 0 = modifiers only
    bool operator==(const KeyRule& o) const {
        return enabled == o.enabled && from_mods == o.from_mods && from_key == o.from_key && to_mods == o.to_mods && to_vk == o.to_vk;
    }
};

// Defaults follow the key labels of a Windows-labelled keyboard used with the
// Mac (its Win key sends Command, Alt sends Option, Ctrl sends Control), so
// each key does on the PC what it says, and the PC keeps its own shortcuts.
struct KeyConfig {
    ModTarget command = ModTarget::Win;
    ModTarget option = ModTarget::Alt;
    ModTarget control = ModTarget::Ctrl;
    std::vector<KeyRule> rules;
    bool operator==(const KeyConfig& o) const { return command == o.command && option == o.option && control == o.control && rules == o.rules; }
};

// As printed on the keys; no shortcut rewriting. The default.
KeyConfig keyboard_layout();
// For an Apple keyboard and Mac habits: Command acts as Ctrl, plus rules such
// as Command-Tab for Alt-Tab and Command-Left for Home.
KeyConfig mac_layout();

struct KeyName {
    const char* name;   // English; translated through tr()
    uint16_t mac;       // kNoMac if the Mac has no such key
    uint16_t vk;        // 0 if Windows has no such key
    bool extended;
};
constexpr uint16_t kNoMac = 0xFFFF;
const std::vector<KeyName>& key_names();
const KeyName* key_by_mac(uint16_t mac);
const KeyName* key_by_vk(uint16_t vk);

// Rule serialisation used by the settings file: "1,1,48,2,9".
std::string rule_to_text(const KeyRule& rule);
bool rule_from_text(const std::string& text, KeyRule& rule);

class KeyMapper {
public:
    using Emit = std::function<void(uint16_t vk, bool extended, bool down)>;
    explicit KeyMapper(Emit emit) : emit_(std::move(emit)) {}

    void set_config(const KeyConfig& config) { release_all(); config_ = config; }
    void key(uint16_t mac, bool down);
    // Lifts everything this mapper pressed (handover, disconnect).
    void release_all();

private:
    struct Out { uint16_t vk; bool ext; bool operator<(const Out& o) const { return vk < o.vk; } };
    Out modifier_out(uint16_t mac) const;
    uint8_t held_mods() const;
    const KeyRule* match(uint16_t mac, uint8_t mods) const;
    void press(Out o);
    void lift(Out o);
    void lift_masked(Out o);
    void begin_rule(const KeyRule& rule, uint16_t source);
    void end_rule(bool restore);
    void fire_tap(const KeyRule& rule);

    Emit emit_;
    KeyConfig config_;
    std::set<uint16_t> physical_;            // Mac keys held down
    std::map<uint16_t, Out> sent_;           // Mac key -> what it pressed
    std::set<Out> down_;                     // Windows keys currently down
    std::set<uint16_t> suppressed_;          // Mac modifiers lifted for a rule
    std::vector<Out> rule_mods_;             // modifiers a rule pressed
    const KeyRule* active_ = nullptr;
    uint16_t active_source_ = 0;
    bool fn_tap_ = false;
};

} // namespace fucairserver::edge
