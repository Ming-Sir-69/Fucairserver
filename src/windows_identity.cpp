#include "custom_identity.h"
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <atomic>
#include <cwchar>
#include <mutex>
#include <string>
#endif

namespace fucairserver {
#ifdef _WIN32
namespace {
constexpr wchar_t tray_class[] = L"Fucairserver.Receiver.Tray.5AC985E5-6D29-47B7-9C20-15889CD902B1";
constexpr GUID tray_guid{0x5ac985e5, 0x6d29, 0x47b7, {0x9c, 0x20, 0x15, 0x88, 0x9c, 0xd9, 0x02, 0xb1}};
constexpr UINT tray_callback = WM_APP + 0x3b0;
constexpr UINT tray_name_changed = WM_APP + 0x3b1;
constexpr UINT tray_shutdown_message = WM_APP + 0x3b2;
constexpr UINT tray_exit_message = WM_APP + 0x3b3;
constexpr UINT_PTR retry_timer = 1;
constexpr UINT menu_open = 1, menu_settings = 2, menu_exit = 3;

struct TrayState {
    std::atomic<HWND> window{nullptr};
    std::atomic_bool available{false};
    std::atomic_bool owns_icon{false};
    std::atomic_bool shutting_down{false};
    std::atomic_bool settings_requested{false};
    std::atomic_bool exit_requested{false};
    std::atomic<DWORD> owner_thread{0};
    SDL_Window* main_sdl_window = nullptr;
    HWND main_window = nullptr;
    HICON icon = nullptr; // LR_SHARED; do not DestroyIcon.
    UINT taskbar_created = 0;
    std::mutex name_mutex;
    std::wstring name = L"Fucairserver";
};

TrayState& tray() { static TrayState state; return state; }

std::wstring from_utf8(std::string const& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring converted(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), converted.data(), count);
    return converted;
}

NOTIFYICONDATAW notification_data() {
    auto& state = tray();
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = state.window.load();
    data.uID = 101;
    data.uFlags = NIF_GUID | NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.guidItem = tray_guid;
    data.uCallbackMessage = tray_callback;
    data.hIcon = state.icon;
    std::wstring tooltip = L"Fucairserver";
    {
        std::lock_guard<std::mutex> lock(state.name_mutex);
        if (!state.name.empty() && state.name != L"Fucairserver") tooltip += L" - " + state.name;
    }
    std::size_t count = (std::min)(tooltip.size(), sizeof(data.szTip) / sizeof(data.szTip[0]) - 1);
    // Preserve a valid UTF-16 boundary when a long custom name is truncated.
    if (count && tooltip[count - 1] >= 0xd800 && tooltip[count - 1] <= 0xdbff) --count;
    std::copy_n(tooltip.data(), count, data.szTip);
    data.szTip[count] = L'\0';
    return data;
}

void delete_icon() {
    auto& state = tray();
    state.available = false;
    // A second process whose NIM_ADD failed must never delete another process's GUID icon.
    if (state.owns_icon.exchange(false)) {
        auto data = notification_data();
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
}

bool add_icon() {
    auto& state = tray();
    if (state.shutting_down || !state.window.load() || !state.icon) return false;
    auto data = notification_data();
    if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
    state.owns_icon = true;
    data.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &data) || state.shutting_down) {
        delete_icon();
        return false;
    }
    state.available = true;
    KillTimer(state.window.load(), retry_timer);
    return true;
}

void restore_main_window() {
    auto& state = tray();
    if (!state.main_sdl_window || !IsWindow(state.main_window)) return;
    SDL_ShowWindow(state.main_sdl_window);
    if (SDL_GetWindowFlags(state.main_sdl_window) & SDL_WINDOW_MINIMIZED)
        SDL_RestoreWindow(state.main_sdl_window);
    SDL_RaiseWindow(state.main_sdl_window);
    ShowWindow(state.main_window, IsIconic(state.main_window) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(state.main_window);
}

void request_exit() {
    auto& state = tray();
    if (state.exit_requested.exchange(true)) return;
    // --quit can arrive while TrackPopupMenuEx owns the render thread.
    // End its modal loop so renderer.stop() can join without user input.
    EndMenu();
    SDL_Event event{};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}

void show_menu(POINT anchor) {
    auto& state = tray();
    const auto owner = state.window.load();
    if (!owner) return;
    NOTIFYICONIDENTIFIER identity{};
    identity.cbSize = sizeof(identity);
    identity.hWnd = owner;
    identity.uID = 101;
    identity.guidItem = tray_guid;
    RECT bounds{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identity, &bounds))) {
        // Explorer's overflow flyout occupies a higher window band than a
        // normal application menu. Anchor above its actual bounds instead
        // of leaving part of the menu underneath the flyout.
        const POINT center{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT flyout{};
        const auto root = GetAncestor(WindowFromPoint(center), GA_ROOT);
        if (root && GetWindowRect(root, &flyout) &&
            flyout.right - flyout.left < (monitor.rcMonitor.right - monitor.rcMonitor.left) / 2 &&
            flyout.bottom - flyout.top < (monitor.rcMonitor.bottom - monitor.rcMonitor.top) / 2)
            bounds.top = flyout.top;
        if (anchor.x == -1) anchor.x = bounds.left;
        anchor.y = (std::min)(bounds.top, monitor.rcWork.bottom) - 4;
    } else if (anchor.x == -1 && anchor.y == -1) {
        GetCursorPos(&anchor);
    }
    const auto menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, menu_open, L"打开 Fucairserver");
    AppendMenuW(menu, MF_STRING, menu_settings, L"设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, menu_exit, L"退出");
    SetMenuDefaultItem(menu, menu_open, FALSE);
    SetForegroundWindow(owner);
    const UINT selected = TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        anchor.x, anchor.y, owner, nullptr);
    DestroyMenu(menu);
    PostMessageW(owner, WM_NULL, 0, 0);
    if (selected == menu_exit) request_exit();
    else if (selected == menu_open || selected == menu_settings) {
        if (selected == menu_settings) state.settings_requested = true;
        restore_main_window();
    } else {
        auto data = notification_data();
        Shell_NotifyIconW(NIM_SETFOCUS, &data);
    }
}

LRESULT CALLBACK tray_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto& state = tray();
    if (state.taskbar_created && message == state.taskbar_created) {
        // Explorer lost the old icon. Re-add it and reapply version 4.
        // Usually Explorer already removed it. Delete our recorded icon first
        // as well, so repeated/spurious broadcasts cannot lose ownership.
        delete_icon();
        if (!add_icon() && !state.shutting_down) SetTimer(window, retry_timer, 2000, nullptr);
        return 0;
    }
    switch (message) {
    case tray_callback: {
        const UINT event = LOWORD(lparam); // NOTIFYICON_VERSION_4 callback layout.
        if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK)
            restore_main_window();
        else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)
            show_menu(POINT{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)});
        return 0;
    }
    case tray_name_changed:
        if (state.available) {
            auto data = notification_data();
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
        return 0;
    case tray_exit_message: case WM_CLOSE: request_exit(); return 0;
    case tray_shutdown_message: shutdown_tray(); return 0;
    case WM_TIMER:
        if (wparam == retry_timer) {
            if (state.shutting_down || state.available) KillTimer(window, retry_timer);
            else add_icon();
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(window, retry_timer);
        delete_icon();
        state.window = nullptr;
        state.owner_thread = 0;
        state.main_sdl_window = nullptr;
        state.main_window = nullptr;
        return 0; // Never PostQuitMessage: SDL owns this thread's message loop.
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void ensure_tray(SDL_Window* sdl_window, HWND native_window, HICON small_icon) {
    auto& state = tray();
    const DWORD thread = GetCurrentThreadId();
    if (state.window.load()) {
        if (state.owner_thread.load() == thread) {
            state.main_sdl_window = sdl_window;
            state.main_window = native_window;
        }
        return;
    }
    if (state.exit_requested) return;
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = tray_window_proc;
    type.hInstance = instance;
    type.lpszClassName = tray_class;
    type.hIcon = small_icon;
    type.hIconSm = small_icon;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    state.shutting_down = false;
    state.main_sdl_window = sdl_window;
    state.main_window = native_window;
    state.icon = small_icon ? small_icon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    state.owner_thread = thread;
    state.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    // A hidden ordinary top-level window receives Explorer's TaskbarCreated
    // broadcast. A message-only HWND would miss it.
    const auto window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, tray_class,
        L"Fucairserver tray controller", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) { state.owner_thread = 0; return; }
    state.window = window;
    if (state.taskbar_created) ChangeWindowMessageFilterEx(window, state.taskbar_created, MSGFLT_ALLOW, nullptr);
    if (!add_icon()) SetTimer(window, retry_timer, 2000, nullptr);
}

struct ExitLookup { std::wstring executable_name; bool posted = false; };
BOOL CALLBACK find_existing_receiver(HWND window, LPARAM context) {
    auto& lookup = *reinterpret_cast<ExitLookup*>(context);
    wchar_t name[256]{};
    if (!GetClassNameW(window, name, static_cast<int>(sizeof(name) / sizeof(name[0]))) || wcscmp(name, tray_class) != 0) return TRUE;
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (!process_id || process_id == GetCurrentProcessId()) return TRUE;
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return TRUE;
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    const bool identified = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    if (!identified) return TRUE;
    const wchar_t* basename = wcsrchr(path, L'\\');
    basename = basename ? basename + 1 : path;
    if (_wcsicmp(basename, lookup.executable_name.c_str()) != 0) return TRUE;
    lookup.posted = PostMessageW(window, tray_exit_message, 0, 0) != FALSE;
    return lookup.posted ? FALSE : TRUE;
}
} // namespace
#endif

void apply_window_identity(SDL_Window* window) {
#ifdef _WIN32
    if (!window) return;
    SetCurrentProcessExplicitAppUserModelID(L"Fucairserver.Receiver");
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
    const auto instance = GetModuleHandleW(nullptr);
    const auto big = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    const auto small = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (big) {
        SendMessageW(info.info.win.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
        SetClassLongPtrW(info.info.win.window, GCLP_HICON, reinterpret_cast<LONG_PTR>(big));
    }
    if (small) {
        SendMessageW(info.info.win.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        SetClassLongPtrW(info.info.win.window, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(small));
    }
    ensure_tray(window, info.info.win.window, small);
#else
    (void)window;
#endif
}

bool tray_available() {
#ifdef _WIN32
    return tray().available.load() && !tray().shutting_down.load();
#else
    return false;
#endif
}

void hide_to_tray(SDL_Window* window) {
#ifdef _WIN32
    if (window && tray_available() && tray().owner_thread.load() == GetCurrentThreadId()) SDL_HideWindow(window);
#else
    (void)window;
#endif
}

bool consume_tray_settings_request() {
#ifdef _WIN32
    return tray().settings_requested.exchange(false);
#else
    return false;
#endif
}

bool tray_exit_requested() {
#ifdef _WIN32
    return tray().exit_requested.load();
#else
    return false;
#endif
}

void update_tray_name(const std::string& name) {
#ifdef _WIN32
    auto& state = tray();
    const auto updated = from_utf8(name);
    {
        std::lock_guard<std::mutex> lock(state.name_mutex);
        if (state.name == updated) return;
        state.name = updated;
    }
    if (const auto window = state.window.load()) PostMessageW(window, tray_name_changed, 0, 0);
#else
    (void)name;
#endif
}

void shutdown_tray() {
#ifdef _WIN32
    auto& state = tray();
    state.shutting_down = true;
    delete_icon();
    const auto window = state.window.load();
    if (!window) return;
    if (state.owner_thread.load() == GetCurrentThreadId()) DestroyWindow(window);
    else PostMessageW(window, tray_shutdown_message, 0, 0);
#endif
}

bool request_existing_exit() {
#ifdef _WIN32
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (!length || length >= sizeof(path) / sizeof(path[0])) return false;
    const wchar_t* basename = wcsrchr(path, L'\\');
    ExitLookup lookup{basename ? basename + 1 : path, false};
    EnumWindows(find_existing_receiver, reinterpret_cast<LPARAM>(&lookup));
    return lookup.posted;
#else
    return false;
#endif
}
}
