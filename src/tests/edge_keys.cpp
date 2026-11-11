#include "edge_keys.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fucairserver::edge;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Recorder {
    std::vector<std::string> events;
    KeyMapper mapper{[this](uint16_t vk, bool, bool down) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%s%02X", down ? "+" : "-", vk);
        events.push_back(buf);
    }};
    std::string take() {
        std::string out;
        for (const auto& e : events) out += (out.empty() ? "" : " ") + e;
        events.clear();
        return out;
    }
};

int main() {
    try {
        // Mac habits on an Apple keyboard.
        const KeyConfig config = mac_layout();

        {   // Default: keys do what a Windows-labelled keyboard says; no rewriting.
            Recorder r; r.mapper.set_config(keyboard_layout());
            r.mapper.key(59, true); r.mapper.key(8, true); r.mapper.key(8, false); r.mapper.key(59, false);
            require(r.take() == "+A2 +43 -43 -A2", "The Ctrl key (Control) must be Ctrl");
            r.mapper.key(55, true); r.mapper.key(2, true); r.mapper.key(2, false); r.mapper.key(55, false);
            require(r.take() == "+5B +44 -44 -5B", "The Win key (Command) must be Win");
            r.mapper.key(58, true); r.mapper.key(48, true); r.mapper.key(48, false); r.mapper.key(58, false);
            require(r.take() == "+A4 +09 -09 -A4", "The Alt key (Option) must give a plain Alt-Tab");
            r.mapper.key(61, true); r.mapper.key(49, true); r.mapper.key(49, false); r.mapper.key(61, false);
            require(r.take() == "+A5 +20 -20 -A5", "Right Alt-Space must reach the PC (voice input)");
            r.mapper.key(55, true); r.mapper.key(123, true); r.mapper.key(123, false); r.mapper.key(55, false);
            require(r.take() == "+5B +25 -25 -5B", "No Mac rules apply by default");
        }

        {   // Plain keys and the default modifier mapping.
            Recorder r; r.mapper.set_config(config);
            r.mapper.key(55, true); r.mapper.key(8, true); r.mapper.key(8, false); r.mapper.key(55, false);
            require(r.take() == "+A2 +43 -43 -A2", "Command-C must become Ctrl-C");
            r.mapper.key(59, true); r.mapper.key(49, true); r.mapper.key(49, false); r.mapper.key(59, false);
            require(r.take() == "+5B +20 -20 -5B", "Control-Space must become Win-Space (input method)");
            r.mapper.key(61, true); r.mapper.key(49, true); r.mapper.key(49, false); r.mapper.key(61, false);
            require(r.take() == "+A5 +20 -20 -A5", "Right Option-Space must become Right Alt-Space (voice input)");
        }
        {   // Command-Tab keeps Alt held across repeated Tabs, then releases it.
            Recorder r; r.mapper.set_config(config);
            r.mapper.key(55, true);
            r.mapper.key(48, true); r.mapper.key(48, false);
            r.mapper.key(48, true); r.mapper.key(48, false);
            r.mapper.key(55, false);
            require(r.take() == "+A2 -A2 +A4 +09 -09 +09 -09 -A4", "Command-Tab must switch apps with Alt-Tab");
        }
        {   // Shift is carried into a rule: Shift-Command-Tab goes backwards.
            Recorder r; r.mapper.set_config(config);
            r.mapper.key(55, true); r.mapper.key(56, true);
            r.mapper.key(48, true); r.mapper.key(48, false);
            r.mapper.key(56, false); r.mapper.key(55, false);
            require(r.take() == "+A2 +A0 -A2 +A4 +09 -09 -A0 -A4", "Shift must be carried into Alt-Tab");
        }
        {   // Releasing Alt for a rule is masked so Windows does not open the menu bar.
            Recorder r; r.mapper.set_config(config);
            r.mapper.key(58, true); r.mapper.key(123, true); r.mapper.key(123, false); r.mapper.key(58, false);
            require(r.take() == "+A4 +E8 -E8 -A4 +A2 +25 -25 -A2", "Option-Left must become Ctrl-Left without a bare Alt tap");
        }
        {   // Moving from one rule to the next under the same modifier.
            Recorder r; r.mapper.set_config(config);
            r.mapper.key(55, true);
            r.mapper.key(123, true); r.mapper.key(123, false);
            r.mapper.key(124, true); r.mapper.key(124, false);
            r.mapper.key(55, false);
            require(r.take() == "+A2 -A2 +24 -24 +23 -23", "Command-Left then Command-Right must be Home then End");
        }
        {   // Command-Up keeps the Ctrl it already holds.
            Recorder r; r.mapper.set_config(config);
            r.mapper.key(55, true); r.mapper.key(126, true); r.mapper.key(126, false); r.mapper.key(55, false);
            require(r.take() == "+A2 +24 -24 -A2", "Command-Up must become Ctrl-Home");
        }
        {   // fn rules fire on a clean tap only.
            KeyConfig fn = config;
            fn.rules.push_back({true, 0, kMacFn, kRightAlt, 0x20});
            Recorder r; r.mapper.set_config(fn);
            r.mapper.key(kMacFn, true); r.mapper.key(kMacFn, false);
            require(r.take() == "+A5 +20 -20 -A5", "A tap on fn must send Right Alt-Space");
            r.mapper.key(kMacFn, true); r.mapper.key(126, true); r.mapper.key(126, false); r.mapper.key(kMacFn, false);
            require(r.take() == "+26 -26", "fn used with another key must not fire its rule");
        }
        {   // Modifier remapping and Caps Lock toggles.
            KeyConfig swapped = config;
            swapped.command = ModTarget::Win;
            Recorder r; r.mapper.set_config(swapped);
            r.mapper.key(54, true); r.mapper.key(54, false);
            require(r.take() == "+5C -5C", "Right Command must follow its configured target");
            r.mapper.key(57, true); r.mapper.key(57, true);
            require(r.take() == "+14 -14 +14 -14", "Each Caps Lock event must toggle once");
        }
        {   // Nothing stays held after a handover.
            Recorder r; r.mapper.set_config(config);
            r.mapper.key(55, true); r.mapper.key(48, true);
            r.take();
            r.mapper.release_all();
            const auto lifted = r.take();
            require(lifted.find("-09") != std::string::npos && lifted.find("-A4") != std::string::npos, "release_all must lift keys");
        }
        {   // Settings text round trip and validation.
            KeyRule rule;
            require(rule_from_text(rule_to_text(config.rules[0]), rule) && rule == config.rules[0], "Rules must round-trip");
            require(!rule_from_text("1,1,999,2,9", rule), "Unknown Mac keys must be rejected");
            require(!rule_from_text("1,1,48,0,0", rule), "A rule must produce something");
        }
        std::cout << "Edge key mapping checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
