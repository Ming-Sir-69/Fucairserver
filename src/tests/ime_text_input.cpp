#define SDL_MAIN_HANDLED
#include "ime_text_input.h"
#include "imgui_internal.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void test_preview() {
    fucairserver::ImePreedit input;
    const std::string saved = u8"随航";
    input.update("nihao", 5, 0);
    auto preview = input.preview(saved, 6, 6, 6);
    require(preview.text == saved + "nihao" && preview.caret == 11, "Pinyin must appear after committed UTF-8 text");
    require(saved == u8"随航", "Preedit must never change the persisted value");
    preview = input.preview(saved, 3, 3, 3);
    require(preview.text == u8"随nihao航", "Preedit must support insertion in the middle");
    preview = input.preview(saved, 6, 6, 0);
    require(preview.text == "nihao", "Preedit must preview replacing a backwards selection");
    input.update(u8"拼音", 1, 1);
    preview = input.preview(saved, 6, 6, 6);
    require(preview.selection_begin == 9 && preview.selection_end == 12, "Preedit cursor and clause offsets must respect UTF-8 boundaries");
    input.update(std::string(512, 'a'), 512, 0);
    require(input.preview("", 0, 0, 0).text.size() == 512, "Extended preedit must not truncate at SDL's 32-byte event limit");
    input.clear();
    require(!input.active() && input.preview(saved, 6, 0, 6).text == saved, "Cancel must restore the unchanged committed display");
}

static void test_input_events() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.f / 60;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    fucairserver::ImeTextInput input;
    std::array<char, 128> value{};
    std::strcpy(value.data(), "receiver");
    auto frame = [&](bool focus = false, bool show = true) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 200));
        ImGui::Begin("Headless input logic");
        if (focus) ImGui::SetKeyboardFocusHere();
        const bool submitted = show && input.input_text("Name", value.data(), value.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::End();
        input.end_frame();
        ImGui::Render();
        return submitted;
    };
    input.begin_events(); frame(true);
    input.begin_events(); frame();
    input.begin_events(); frame();
    auto* state = ImGui::GetInputTextState(ImGui::GetActiveID());
    require(state != nullptr, "Headless input must acquire focus");
    state->ReloadUserBufAndMoveToEnd();
    input.begin_events(); frame();

    SDL_Event edit{};
    edit.type = SDL_TEXTEDITING; edit.edit.windowID = 1;
    std::strcpy(edit.edit.text, "nihao"); edit.edit.start = 5;
    input.begin_events();
    require(input.handle_event(edit, 1) && input.composing(), "Editing events must enable the inline preview");
    require(!frame() && std::string(value.data()) == "receiver", "Preview must not submit or save pinyin");
    SDL_Event enter{};
    enter.type = SDL_KEYDOWN; enter.key.windowID = 1; enter.key.keysym.sym = SDLK_RETURN;
    input.begin_events();
    require(input.handle_event(enter, 1), "IME Enter must not activate Apply name");
    require(!frame() && ImGui::GetActiveID() != 0, "IME Enter must keep editing focused");

    SDL_Event commit{};
    commit.type = SDL_TEXTINPUT; commit.text.windowID = 1;
    std::strcpy(commit.text.text, u8"你好");
    input.begin_events();
    require(!input.handle_event(commit, 1) && !input.composing(), "Committed text must continue to the standard backend");
    io.AddInputCharactersUTF8(commit.text.text); // Same committed-character path as imgui_impl_sdl2.
    require(!frame() && std::string(value.data()) == u8"receiver你好", "A candidate must commit exactly once");

    input.begin_events();
    require(!input.handle_event(enter, 1), "A later ordinary Enter must still submit");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    require(frame(), "Normal Enter must retain the existing Apply name behavior");
    io.AddKeyEvent(ImGuiKey_Enter, false);
    input.begin_events(); frame(true);
    input.begin_events(); frame();
    input.begin_events(); frame();
    const std::string committed = value.data();
    input.begin_events(); input.handle_event(edit, 1); frame();
    edit.edit.text[0] = '\0'; edit.edit.start = 0;
    input.begin_events(); input.handle_event(edit, 1); frame();
    require(!input.composing() && std::string(value.data()) == committed, "Cancel must discard preedit without changing committed text");

    SDL_Event extended{};
    extended.type = SDL_TEXTEDITING_EXT; extended.editExt.windowID = 1;
    extended.editExt.text = SDL_strdup(std::string(128, 'a').c_str());
    extended.editExt.start = 128;
    input.begin_events(); input.handle_event(extended, 1); frame();
    require(input.composing() && std::string(value.data()) == committed, "Extended events must remain preview-only");
    input.begin_events(); frame(false, false);
    require(!input.composing(), "Closing the settings field must cancel stale composition");
    ImGui::DestroyContext();
}

int main() {
    try {
        test_preview();
        test_input_events();
        std::cout << "IME UTF-8 preview, selection, commit, cancel and Enter handling passed without creating a desktop window\n";
        return 0;
    } catch (const std::exception& error) {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
