#include "ime_text_input.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace fucairserver {
void ImeTextInput::begin_events() {
    submitted_ = false;
    activity_ = preedit_.active();
}

void ImeTextInput::clear(bool cancel_native) {
    const bool was_active = preedit_.active();
    preedit_.clear();
    owner_ = 0;
    scroll_ = 0;
    if (was_active && cancel_native) SDL_ClearComposition();
}

void ImeTextInput::update(const char* text, int cursor, int selection) {
    activity_ = true;
    if (!field_ || ImGui::GetActiveID() != field_) { clear(); return; }
    if (!preedit_.active()) {
        const auto* input = ImGui::GetInputTextState(field_);
        scroll_ = input ? input->Scroll.x : 0;
    }
    owner_ = field_;
    preedit_.update(text ? text : "", cursor, selection);
    if (!preedit_.active()) clear();
}

bool ImeTextInput::handle_event(const SDL_Event& event, Uint32 window_id) {
    if (event.type == SDL_TEXTEDITING) {
        if (event.edit.windowID == window_id) update(event.edit.text, event.edit.start, event.edit.length);
        return true;
    }
    if (event.type == SDL_TEXTEDITING_EXT) {
        if (event.editExt.windowID == window_id) update(event.editExt.text, event.editExt.start, event.editExt.length);
        SDL_free(event.editExt.text);
        return true;
    }
    if (event.type == SDL_TEXTINPUT && event.text.windowID == window_id) {
        activity_ = activity_ || preedit_.active();
        clear();
        return false; // Commit exactly once through ImGui_ImplSDL2_ProcessEvent.
    }
    if (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id &&
        event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        clear();
        field_ = 0;
    }
    if (event.type == SDL_KEYDOWN && event.key.windowID == window_id && activity_ &&
        field_ && ImGui::GetActiveID() == field_) {
        switch (event.key.keysym.sym) {
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_ESCAPE: case SDLK_TAB:
        case SDLK_BACKSPACE: case SDLK_DELETE: case SDLK_SPACE:
        case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN:
        case SDLK_HOME: case SDLK_END: case SDLK_PAGEUP: case SDLK_PAGEDOWN:
            return true; // IME navigation must not edit committed text or submit a name.
        default: break;
        }
    }
    return false;
}

bool ImeTextInput::input_text(const char* label, char* buffer, std::size_t capacity, ImGuiInputTextFlags flags) {
    const bool result = ImGui::InputText(label, buffer, capacity, flags);
    if (ImGui::IsItemActive()) {
        const auto id = ImGui::GetItemID();
        if (preedit_.active() && owner_ != id) clear(true);
        field_ = id;
        submitted_ = true;
        if (preedit_.active()) {
            if (ImGui::IsItemVisible()) draw();
            else clear(true);
        }
    }
    return result && !((flags & ImGuiInputTextFlags_EnterReturnsTrue) && activity_);
}

void ImeTextInput::end_frame() {
    if (!submitted_ || ImGui::GetActiveID() != field_) {
        clear(true);
        field_ = 0;
    }
}

void ImeTextInput::draw() {
    const auto* input = ImGui::GetInputTextState(owner_);
    if (!input) { clear(true); return; }
    const auto preview = preedit_.preview(std::string_view(input->TextA.Data, input->TextLen),
        input->GetCursorPos(), input->GetSelectionStart(), input->GetSelectionEnd());
    const auto first = ImGui::GetItemRectMin(), last = ImGui::GetItemRectMax();
    const auto& style = ImGui::GetStyle();
    const ImVec2 origin(first.x + style.FramePadding.x, first.y + style.FramePadding.y);
    const ImVec2 limit(last.x - style.FramePadding.x, last.y - 1);
    const float width = std::max(1.f, limit.x - origin.x);
    auto extent = [&](std::size_t byte) {
        return ImGui::CalcTextSize(preview.text.data(), preview.text.data() + byte, false).x;
    };
    const float caret = extent(preview.caret);
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, extent(preview.text.size()) - width + 1));
    if (caret - scroll_ > width - 1) scroll_ = caret - width + 1;
    if (caret < scroll_) scroll_ = caret;
    const ImVec2 position(origin.x - scroll_, origin.y);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(first.x + 1, first.y + 1), ImVec2(last.x - 1, last.y - 1),
        ImGui::GetColorU32(ImGuiCol_FrameBg), std::max(0.f, style.FrameRounding - 1));
    draw->PushClipRect(ImVec2(origin.x, first.y + 1), limit, true);
    if (preview.selection_end > preview.selection_begin) {
        draw->AddRectFilled(ImVec2(position.x + extent(preview.selection_begin), origin.y),
            ImVec2(position.x + extent(preview.selection_end), std::min(limit.y, origin.y + ImGui::GetFontSize() + 1)), ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
    }
    draw->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), preview.text.c_str());
    const float underline = std::min(limit.y - 1, origin.y + ImGui::GetFontSize() + 1);
    const auto accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    draw->AddLine(ImVec2(position.x + extent(preview.begin), underline),
        ImVec2(position.x + extent(preview.end), underline), accent, 1);
    const ImVec2 cursor(std::floor(position.x + caret), origin.y);
    draw->AddLine(ImVec2(cursor.x, cursor.y + 1), ImVec2(cursor.x, underline), ImGui::GetColorU32(ImGuiCol_Text));
    draw->PopClipRect();
    // Keep the native candidate list next to the displayed preedit caret.
    auto& data = ImGui::GetCurrentContext()->PlatformImeData;
    data.WantVisible = true;
    data.InputPos = ImVec2(cursor.x - 1, cursor.y);
    data.InputLineHeight = ImGui::GetFontSize();
}
}
