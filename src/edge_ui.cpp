#include "edge_ui.h"

#include "edge_control.h"
#include "ui_text.h"
#include "video/receiver_ui.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace fucairserver {
namespace {

namespace ui_draw = ap::video::receiver_ui;
using namespace edge;

void hint(ImFont* small_font, const char* text) {
    ImGui::PushFont(small_font);
    ImGui::PushStyleColor(ImGuiCol_Text, ui_draw::vec(ui_draw::muted));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void heading(ImFont* bold_font, const char* text) {
    ImGui::Dummy(ImVec2(0, 14));
    ImGui::PushFont(bold_font);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
}

std::string key_label(uint16_t code, bool mac) {
    const KeyName* k = mac ? key_by_mac(code) : key_by_vk(code);
    if (!k) return mac ? "?" : tr("(none)");
    return tr(k->name);
}

bool key_combo(const char* id, uint16_t& value, bool mac) {
    bool changed = false;
    ImGui::SetNextItemWidth(130);
    if (ImGui::BeginCombo(id, key_label(value, mac).c_str(), ImGuiComboFlags_HeightLarge)) {
        if (!mac && ImGui::Selectable(tr("(none)"), value == 0)) { value = 0; changed = true; }
        for (const auto& k : key_names()) {
            const uint16_t code = mac ? k.mac : k.vk;
            if (mac ? code == kNoMac : code == 0) continue;
            if (ImGui::Selectable(tr(k.name), value == code)) { value = code; changed = true; }
            if (value == code) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool mod_toggle(const char* label, uint8_t& mods, uint8_t bit) {
    bool on = (mods & bit) != 0;
    if (!ImGui::Checkbox(label, &on)) return false;
    mods = on ? uint8_t(mods | bit) : uint8_t(mods & ~bit);
    return true;
}

bool target_combo(const char* id, ModTarget& target) {
    const char* names[] = {"Ctrl", "Alt", tr("Windows key")};
    int v = int(target);
    ImGui::SetNextItemWidth(140);
    if (!ImGui::Combo(id, &v, names, 3)) return false;
    target = ModTarget(v);
    return true;
}

} // namespace

void draw_edge_page(EdgeControl& edge, ImFont* bold_font, ImFont* small_font, float top, float width) {
    Config cfg = edge.config();
    const Status st = edge.status();
    bool changed = false;

    ImGui::SetCursorPos(ImVec2(42, top));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);

    changed |= ImGui::Checkbox(tr("Let a paired Mac use its keyboard and mouse here"), &cfg.enabled);
    changed |= ImGui::Checkbox(tr("Focus follows the pointer: while it is on the Mac, text entered on this PC, such as voice input, goes to the Mac"), &cfg.send_text);
    std::string controlling;
    int online = 0;
    for (const auto& d : st.devices) { if (d.controlling) controlling = d.name; if (d.online) ++online; }
    char line[256];
    if (!st.listening) std::snprintf(line, sizeof(line), "%s", tr(st.problem.empty() ? "Edge control is not running." : st.problem.c_str()));
    else if (!cfg.enabled) std::snprintf(line, sizeof(line), "%s", tr("Turned off. Paired Macs stay paired."));
    else if (!controlling.empty()) std::snprintf(line, sizeof(line), tr("In use by %s"), controlling.c_str());
    else if (online) std::snprintf(line, sizeof(line), "%s", tr("Mac connected. Push the pointer past the edge of the Mac's screen to come here."));
    else std::snprintf(line, sizeof(line), "%s", tr("Waiting for a paired Mac."));
    hint(small_font, line);
    hint(small_font, tr("Press Control-Option-Command-Esc on the Mac to take the pointer back at any time. Paused while the Mac mirrors its screen to this PC."));

    heading(bold_font, tr("Paired Macs"));
    if (st.devices.empty()) {
        hint(small_font, tr("No Mac yet. On the Mac, open FucairEdge in the menu bar and choose Pair with this PC. Both screens show a code; confirm on both if they match."));
    } else if (ImGui::BeginTable("##edge_devices", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& d : st.devices) {
            ImGui::PushID(d.id.c_str());
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 40);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5, p.y + ImGui::GetTextLineHeight() / 2 + 2), 4,
                d.online ? IM_COL32(44, 161, 116, 255) : ui_draw::faint);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16);
            ImGui::TextUnformatted(d.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(d.controlling ? "In use" : d.online ? "Connected" : "Offline"));
            bool control = d.control, clipboard = d.clipboard;
            ImGui::TableNextColumn();
            if (ImGui::Checkbox(tr("Keyboard & mouse"), &control)) edge.set_device(d.id, control, clipboard);
            ImGui::TableNextColumn();
            if (ImGui::Checkbox(tr("Clipboard"), &clipboard)) edge.set_device(d.id, control, clipboard);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(tr("Remove"))) ImGui::OpenPopup("##remove");
            if (ImGui::BeginPopup("##remove")) {
                ImGui::TextUnformatted(tr("Remove this Mac? It has to pair again to connect."));
                if (ImGui::Button(tr("Remove"))) { edge.remove_device(d.id); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    heading(bold_font, tr("Clipboard sharing"));
    changed |= ImGui::Checkbox(tr("Share the clipboard with paired Macs"), &cfg.clipboard);
    hint(small_font, tr("Text and images, in both directions, while the Mac is connected. Content that password managers mark as private is never shared."));

    heading(bold_font, tr("Modifier keys"));
    hint(small_font, tr("What the Mac's modifier keys do on this PC. The default follows a Windows-labelled keyboard: its Win key (Command on the Mac) is Win, Alt (Option) is Alt and Ctrl (Control) is Ctrl. Left and right keys stay apart."));
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Command"); ImGui::SameLine(110);
    changed |= target_combo("##command", cfg.keys.command);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Option"); ImGui::SameLine(110);
    changed |= target_combo("##option", cfg.keys.option);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Control"); ImGui::SameLine(110);
    changed |= target_combo("##control", cfg.keys.control);

    heading(bold_font, tr("Shortcut mappings"));
    hint(small_font, tr("Turn a Mac shortcut into a different one on this PC. A held Shift is passed on unless the rule names it. A rule on fn (Globe) fires when fn is tapped alone."));
    int remove_at = -1;
    if (ImGui::BeginTable("##edge_rules", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn(tr("On"));
        ImGui::TableSetupColumn(tr("On the Mac"));
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn(tr("On this PC"));
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < cfg.keys.rules.size(); ++i) {
            KeyRule& r = cfg.keys.rules[i];
            ImGui::PushID(int(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##on", &r.enabled);
            ImGui::TableNextColumn();
            changed |= mod_toggle("Cmd", r.from_mods, kCommand); ImGui::SameLine();
            changed |= mod_toggle("Opt", r.from_mods, kOption); ImGui::SameLine();
            changed |= mod_toggle("Ctrl##m", r.from_mods, kControl); ImGui::SameLine();
            changed |= mod_toggle("Shift##m", r.from_mods, kShift); ImGui::SameLine();
            changed |= key_combo("##from", r.from_key, true);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("->");
            ImGui::TableNextColumn();
            changed |= mod_toggle("Ctrl##w", r.to_mods, kCtrl); ImGui::SameLine();
            changed |= mod_toggle("Alt", r.to_mods, kAlt); ImGui::SameLine();
            changed |= mod_toggle(tr("Right Alt"), r.to_mods, kRightAlt); ImGui::SameLine();
            changed |= mod_toggle("Win", r.to_mods, kWin); ImGui::SameLine();
            changed |= mod_toggle("Shift##w", r.to_mods, kWinShift); ImGui::SameLine();
            changed |= key_combo("##to", r.to_vk, false);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(tr("Remove"))) remove_at = int(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove_at >= 0) { cfg.keys.rules.erase(cfg.keys.rules.begin() + remove_at); changed = true; }
    if (ui_draw::text_button(tr("Add mapping"), ImVec2(130, 32))) {
        cfg.keys.rules.push_back({true, kCommand, 0, kCtrl, 'A'});
        changed = true;
    }
    ImGui::SameLine();
    if (ui_draw::text_button(tr("As labelled (default)"), ImVec2(170, 32))) {
        cfg.keys = keyboard_layout();
        changed = true;
    }
    ImGui::SameLine();
    if (ui_draw::text_button(tr("Mac habits"), ImVec2(130, 32))) {
        cfg.keys = mac_layout();
        changed = true;
    }
    ImGui::Dummy(ImVec2(0, 24));
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    // A rule without any output would swallow its keys; keep it out until completed.
    if (changed) {
        Config valid = cfg;
        for (auto& r : valid.keys.rules) if (r.to_vk == 0 && r.to_mods == 0) r.enabled = false;
        edge.set_config(valid);
    }
}

} // namespace fucairserver
