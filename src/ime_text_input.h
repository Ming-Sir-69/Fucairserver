#pragma once
#include "ime_preedit.h"
#include "imgui.h"
#include <SDL.h>

namespace fucairserver {
class ImeTextInput {
public:
    void begin_events();
    // True consumes preedit events or keys already handled by the IME.
    // Committed text always continues to the unmodified ImGui SDL backend.
    bool handle_event(const SDL_Event& event, Uint32 window_id);
    bool input_text(const char* label, char* buffer, std::size_t capacity, ImGuiInputTextFlags flags = 0);
    void end_frame();
    bool composing() const { return preedit_.active(); }
private:
    void clear(bool cancel_native = false);
    void update(const char* text, int cursor, int selection);
    void draw();
    ImePreedit preedit_;
    ImGuiID field_ = 0, owner_ = 0;
    bool submitted_ = false, activity_ = false;
    float scroll_ = 0;
};
}
