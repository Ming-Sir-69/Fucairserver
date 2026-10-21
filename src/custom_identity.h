#pragma once
#include <string>
struct SDL_Window;
namespace fucairserver {
// Identity and tray lifecycle belong to the SDL window/render thread.
void apply_window_identity(SDL_Window* window);
bool tray_available();
void hide_to_tray(SDL_Window* window);
bool consume_tray_settings_request();
bool tray_exit_requested();
// Name updates may be issued from another thread; shell work is posted to SDL.
void update_tray_name(const std::string& name);
void shutdown_tray();
// Used by a separate --quit invocation; targets only this app's tray class.
bool request_existing_exit();
}
