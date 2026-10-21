// Independent Win32 Miracast host. Receive/play flow adapted from the MIT
// KevinKennedy/MiracastReceiverSample; see THIRD_PARTY_NOTICES.md.
#include <windows.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Miracast.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.System.h>
#include "platform_interop.h"
#include "resource.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Media::Miracast;
using namespace Windows::Media::Playback;
using namespace Windows::UI::Composition;
constexpr UINT task_message = WM_APP + 21;
constexpr UINT queue_closed_message = WM_APP + 22;
constexpr UINT close_timer = 1;
constexpr UINT smoke_timer = 2;
constexpr int start_id = 201, stop_id = 202;

std::wstring hex_code(std::int32_t value) {
    std::wostringstream out;
    out << L"0x" << std::hex << std::uppercase << std::setfill(L'0') << std::setw(8)
        << static_cast<std::uint32_t>(value);
    return out.str();
}
std::wstring error_text(hresult_error const& error) {
    return hex_code(error.code().value) + L" " + std::wstring(error.message());
}
std::wstring listening_name(MiracastReceiverListeningStatus value) {
    switch (value) {
    case MiracastReceiverListeningStatus::NotListening: return L"Not listening";
    case MiracastReceiverListeningStatus::Listening: return L"Listening";
    case MiracastReceiverListeningStatus::ConnectionPending: return L"Connection pending";
    case MiracastReceiverListeningStatus::Connected: return L"Connected";
    case MiracastReceiverListeningStatus::DisabledByPolicy: return L"Disabled by policy";
    case MiracastReceiverListeningStatus::TemporarilyDisabled: return L"Temporarily disabled";
    }
    return L"Unknown (" + std::to_wstring(static_cast<int>(value)) + L")";
}
std::wstring start_name(MiracastReceiverSessionStartStatus value) {
    switch (value) {
    case MiracastReceiverSessionStartStatus::Success: return L"Success";
    case MiracastReceiverSessionStartStatus::MiracastNotSupported: return L"Miracast not supported";
    case MiracastReceiverSessionStartStatus::AccessDenied: return L"Access denied";
    default: return L"Unknown failure";
    }
}
std::string json_string(std::wstring const& value) {
    const auto utf8 = to_string(value);
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : utf8) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
            else out << c;
        }
    }
    out << '"';
    return out.str();
}

struct Options {
    bool probe = false;
    bool ui_smoke = false;
    bool start = false;
    bool log_to_file = false;
    std::wstring name = L"Fucairserver";
    std::filesystem::path report;
};
Options parse_options() {
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv) throw_last_error();
    Options options;
    for (int i = 1; i < count; ++i) {
        const std::wstring item = argv[i];
        if (item == L"--probe") options.probe = true;
        else if (item == L"--ui-smoke") options.ui_smoke = true;
        else if (item == L"--start") options.start = true;
        else if (item == L"--log") options.log_to_file = true;
        else if ((item == L"--name" || item == L"--report") && i + 1 < count) {
            if (item == L"--name") options.name = argv[++i];
            else options.report = argv[++i];
        } else { LocalFree(argv); throw hresult_invalid_argument(L"Unknown or incomplete command line option"); }
    }
    LocalFree(argv);
    if ((options.probe || options.ui_smoke) && options.start)
        throw hresult_invalid_argument(L"A read-only test cannot be combined with --start");
    return options;
}

// Poll only on the receiver's MTA worker. Cancellation also lets a closing
// window retire an incomplete Windows operation without blocking its UI.
template <typename Operation>
auto wait_operation(Operation const& operation, std::atomic_bool const& stop) {
    const auto began = GetTickCount64();
    while (operation.Status() == AsyncStatus::Started) {
        if (stop.load()) { operation.Cancel(); throw hresult_canceled(); }
        if (GetTickCount64() - began > 45000) {
            operation.Cancel();
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Windows Miracast operation timed out");
        }
        Sleep(25);
    }
    return operation.GetResults();
}

int probe(Options const& options) {
    init_apartment(apartment_type::multi_threaded);
    std::ostringstream out;
    int result = 0;
    std::wstring stage = L"class activation";
    out << "{\n  \"operation\": \"read-only class activation and capability query\",\n"
        << "  \"receiverStarted\": false,\n  \"settingsApplied\": false,\n";
    try {
        MiracastReceiver receiver;
        std::atomic_bool stop{false};
        stage = L"GetStatusAsync";
        auto status = wait_operation(receiver.GetStatusAsync(), stop);
        stage = L"GetCurrentSettingsAsync";
        auto settings = wait_operation(receiver.GetCurrentSettingsAsync(), stop);
        out << "  \"success\": true,\n  \"wifiStatus\": " << static_cast<int>(status.WiFiStatus())
            << ",\n  \"listeningStatus\": " << static_cast<int>(status.ListeningStatus())
            << ",\n  \"listeningName\": " << json_string(listening_name(status.ListeningStatus()))
            << ",\n  \"maxSimultaneousConnections\": " << status.MaxSimultaneousConnections()
            << ",\n  \"connectionTakeoverSupported\": " << (status.IsConnectionTakeoverSupported() ? "true" : "false")
            << ",\n  \"currentFriendlyName\": " << json_string(std::wstring(settings.FriendlyName())) << '\n';
    } catch (hresult_error const& error) {
        result = 1;
        out << "  \"success\": false,\n  \"hresult\": " << json_string(hex_code(error.code().value))
            << ",\n  \"stage\": " << json_string(stage)
            << ",\n  \"error\": " << json_string(std::wstring(error.message())) << '\n';
    }
    out << "}\n";
    if (!options.report.empty()) {
        if (options.report.has_parent_path()) std::filesystem::create_directories(options.report.parent_path());
        std::ofstream file(options.report, std::ios::binary);
        file << out.str();
        if (!file) throw std::runtime_error("Cannot write probe report");
    }
    std::cout << out.str();
    uninit_apartment();
    return result;
}

struct DeferralCompletion {
    Deferral value{nullptr};
    explicit DeferralCompletion(Deferral token) : value(std::move(token)) {}
    ~DeferralCompletion() { try { if (value) value.Complete(); } catch (...) {} }
};

struct App : std::enable_shared_from_this<App> {
    HWND window{}, video{}, status_label{}, name_edit{}, log_edit{}, start_button{}, stop_button{};
    Options options;
    std::atomic_bool stop_requested{false}, closing{false}, worker_finished{true}, status_dirty{false};
    std::atomic<std::uint64_t> generation{0};
    std::thread worker;
    std::mutex inbox_mutex;
    std::deque<std::function<void()>> inbox;
    std::ofstream log_file;
    Windows::System::DispatcherQueueController queue{nullptr};
    Windows::Foundation::IAsyncAction queue_shutdown{nullptr};
    bool queue_shutdown_started = false;
    bool queue_shutdown_completed = false;
    Compositor compositor{nullptr};
    Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    ContainerVisual root{nullptr};
    SpriteVisual image{nullptr};
    MediaPlayer player{nullptr};
    MediaPlayerSurface player_surface{nullptr};

    explicit App(Options value) : options(std::move(value)) {}
    ~App() { if (worker.joinable()) { stop_requested = true; worker.join(); } }

    bool post(std::function<void()> action) {
        std::lock_guard<std::mutex> lock(inbox_mutex);
        if (closing || !window) return false;
        inbox.push_back(std::move(action));
        PostMessageW(window, task_message, 0, 0);
        return true;
    }
    void send_log(std::wstring message) {
        auto weak = weak_from_this();
        post([weak, message = std::move(message)] { if (auto self = weak.lock()) self->log(message); });
    }
    void log(std::wstring const& message) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::wostringstream line;
        line << std::setfill(L'0') << std::setw(2) << time.wHour << L':' << std::setw(2) << time.wMinute
             << L':' << std::setw(2) << time.wSecond << L"  " << message << L"\r\n";
        const auto text = line.str();
        if (GetWindowTextLengthW(log_edit) > 60000) SetWindowTextW(log_edit, L"");
        SendMessageW(log_edit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(log_edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
        if (log_file.is_open()) {
            log_file << to_string(text);
            log_file.flush();
        }
    }
    void set_status(std::wstring const& text) { SetWindowTextW(status_label, text.c_str()); log(text); }
    void drain_tasks() {
        std::deque<std::function<void()>> tasks;
        { std::lock_guard<std::mutex> lock(inbox_mutex); tasks.swap(inbox); }
        if (closing) return;
        for (auto& task : tasks) {
            try { task(); }
            catch (hresult_error const& error) { set_status(L"Error: " + error_text(error)); }
            catch (std::exception const& error) { set_status(L"Error: " + std::wstring(to_hstring(error.what()))); }
        }
    }
    HWND control(wchar_t const* kind, wchar_t const* text, DWORD style, int id = 0) {
        auto child = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!child) throw_last_error();
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return child;
    }
    void initialize(HWND hwnd) {
        window = hwnd;
        name_edit = control(L"EDIT", options.name.c_str(), WS_BORDER | ES_AUTOHSCROLL);
        start_button = control(L"BUTTON", L"Start receiver", BS_PUSHBUTTON | WS_TABSTOP, start_id);
        stop_button = control(L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, stop_id);
        EnableWindow(stop_button, FALSE);
        status_label = control(L"STATIC", L"Ready", SS_LEFT);
        video = control(L"STATIC", L"", SS_BLACKRECT);
        log_edit = control(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL);
        if (options.log_to_file) {
            wchar_t module_path[32768]{};
            GetModuleFileNameW(nullptr, module_path, 32768);
            auto log_dir = std::filesystem::path(module_path).parent_path() / L"logs";
            std::filesystem::create_directories(log_dir);
            log_file.open(log_dir / (L"miracast-" + std::to_wstring(GetCurrentProcessId()) + L".log"), std::ios::binary | std::ios::app);
        }
        queue = create_dispatcher_queue();
        compositor = Compositor();
        target = create_window_target(compositor, video);
        root = compositor.CreateContainerVisual();
        root.RelativeSizeAdjustment({1.0f, 1.0f});
        image = compositor.CreateSpriteVisual();
        image.RelativeSizeAdjustment({1.0f, 1.0f});
        root.Children().InsertAtTop(image);
        target.Root(root);
        log(L"Fucairserver");
        log(L"Connect from Wireless Display on Windows or a compatible Android device.");
        set_status(L"Ready. Start receiver to advertise this PC over Miracast.");
        layout();
        if (options.start) PostMessageW(window, WM_COMMAND, start_id, 0);
        if (options.ui_smoke && !SetTimer(window, smoke_timer, 500, nullptr)) throw_last_error();
    }
    void layout() {
        if (!window) return;
        RECT area{};
        GetClientRect(window, &area);
        const int width = area.right, height = area.bottom;
        MoveWindow(name_edit, 10, 10, 220, 25, TRUE);
        MoveWindow(start_button, 240, 9, 130, 27, TRUE);
        MoveWindow(stop_button, 380, 9, 75, 27, TRUE);
        MoveWindow(status_label, 465, 13, (std::max)(width - 475, 1), 24, TRUE);
        const int log_height = 135;
        MoveWindow(video, 10, 45, (std::max)(width - 20, 1), (std::max)(height - log_height - 65, 1), TRUE);
        MoveWindow(log_edit, 10, (std::max)(height - log_height - 10, 50), (std::max)(width - 20, 1), log_height, TRUE);
    }
    void clear_playback() {
        if (image) image.Brush(nullptr);
        if (player) {
            try { player.Pause(); player.Source(nullptr); player.Close(); } catch (...) {}
            player = nullptr;
        }
        player_surface = nullptr;
    }
    void attach_source(Windows::Media::Core::MediaSource const& source, std::uint64_t run) {
        if (closing || stop_requested || run != generation.load()) return;
        clear_playback();
        player = MediaPlayer();
        player.RealTimePlayback(true);
        player.AutoPlay(false);
        auto weak = weak_from_this();
        player.MediaOpened([weak, run](auto const&, auto const&) {
            if (auto self = weak.lock()) self->post([weak, run] {
                if (auto app = weak.lock(); app && app->generation.load() == run && !app->stop_requested)
                    app->set_status(L"Media opened; Miracast audio/video playback requested.");
            });
        });
        player.MediaFailed([weak, run](auto const&, auto const& args) {
            if (auto self = weak.lock()) {
                const auto message = L"Media playback failed: " + hex_code(args.ExtendedErrorCode().value)
                    + L" " + std::wstring(args.ErrorMessage());
                self->post([weak, run, message] {
                    if (auto app = weak.lock(); app && app->generation.load() == run) app->set_status(message);
                });
            }
        });
        player.Source(source);
        player_surface = player.GetSurface(compositor);
        auto brush = compositor.CreateSurfaceBrush(player_surface.CompositionSurface());
        brush.Stretch(CompositionStretch::Uniform);
        image.Brush(brush);
        player.Play();
        set_status(L"Miracast media source attached; waiting for the first playable frame.");
    }
    void start_receiver() {
        if (!worker_finished || closing) return;
        if (worker.joinable()) worker.join();
        std::wstring friendly(static_cast<std::size_t>(GetWindowTextLengthW(name_edit)) + 1, L'\0');
        GetWindowTextW(name_edit, friendly.data(), static_cast<int>(friendly.size()));
        friendly.resize(wcslen(friendly.c_str()));
        if (friendly.empty() || friendly.size() > 50) { set_status(L"Enter a receiver name containing 1 to 50 characters."); return; }
        const auto run = ++generation;
        stop_requested = false;
        worker_finished = false;
        EnableWindow(start_button, FALSE);
        EnableWindow(name_edit, FALSE);
        EnableWindow(stop_button, TRUE);
        set_status(L"Starting Windows Miracast receiver...");
        auto self = shared_from_this();
        worker = std::thread([self, friendly = std::move(friendly), run] { self->receive(friendly, run); });
    }
    void stop_receiver() {
        if (worker_finished) return;
        stop_requested = true;
        clear_playback();
        EnableWindow(stop_button, FALSE);
        set_status(L"Stopping receiver...");
    }
    void receive(std::wstring const& friendly, std::uint64_t run) {
        bool apartment_initialized = false;
        std::wstring final_status = L"Receiver stopped.";
        MiracastReceiver receiver{nullptr};
        MiracastReceiverSession session{nullptr};
        event_token changed{}, connected{}, disconnected{}, source_created{};
        bool have_changed = false, have_session_events = false;
        try {
            init_apartment(apartment_type::multi_threaded);
            apartment_initialized = true;
            receiver = MiracastReceiver();
            auto weak = weak_from_this();
            changed = receiver.StatusChanged([weak](auto const&, auto const&) { if (auto app = weak.lock()) app->status_dirty = true; });
            have_changed = true;
            auto settings = receiver.GetDefaultSettings();
            settings.FriendlyName(friendly);
            settings.AuthorizationMethod(MiracastReceiverAuthorizationMethod::None);
            const auto applied = wait_operation(receiver.DisconnectAllAndApplySettingsAsync(settings), stop_requested);
            if (static_cast<int>(applied.Status()) != 0) {
                const auto error = applied.ExtendedError();
                throw hresult_error(error.value < 0 ? error : hresult{E_FAIL}, L"Windows could not apply Miracast settings; status " + std::to_wstring(static_cast<int>(applied.Status())));
            }
            if (stop_requested) throw hresult_canceled();
            session = wait_operation(receiver.CreateSessionAsync(nullptr), stop_requested);
            session.AllowConnectionTakeover(true);
            session.MaxSimultaneousConnections(1);
            connected = session.ConnectionCreated([weak, run](auto const&, auto const& args) {
                if (auto app = weak.lock()) {
                    try {
                        const std::wstring name(args.Connection().Transmitter().Name());
                        const std::wstring pin(args.Pin());
                        app->post([weak, run, name, pin] {
                            if (auto self = weak.lock(); self && self->generation.load() == run && !self->stop_requested) {
                                self->set_status(L"Connecting: " + name);
                                if (!pin.empty()) self->log(L"Miracast PIN: " + pin);
                            }
                        });
                    } catch (hresult_error const& error) { app->send_log(L"Connection event: " + error_text(error)); }
                }
            });
            disconnected = session.Disconnected([weak, run](auto const&, auto const&) {
                if (auto app = weak.lock()) app->post([weak, run] {
                    if (auto self = weak.lock(); self && self->generation.load() == run && !self->stop_requested) {
                        self->clear_playback();
                        self->set_status(L"Disconnected. Waiting for a Miracast transmitter.");
                    }
                });
            });
            source_created = session.MediaSourceCreated([weak, run](auto const&, auto const& args) {
                auto completion = std::make_shared<DeferralCompletion>(args.GetDeferral());
                if (auto app = weak.lock()) {
                    auto source = args.MediaSource();
                    app->post([weak, source, run, completion] {
                        if (auto self = weak.lock()) self->attach_source(source, run);
                    });
                }
            });
            have_session_events = true;
            const auto started = wait_operation(session.StartAsync(), stop_requested);
            send_log(L"StartAsync: " + start_name(started.Status()) + L"; extended error " + hex_code(started.ExtendedError().value));
            if (started.Status() != MiracastReceiverSessionStartStatus::Success) {
                const auto error = started.ExtendedError();
                throw hresult_error(error.value < 0 ? error : hresult{E_FAIL}, L"Miracast start: " + start_name(started.Status()));
            }
            status_dirty = true;
            while (!stop_requested) {
                if (status_dirty.exchange(false)) {
                    const auto current = wait_operation(receiver.GetStatusAsync(), stop_requested);
                    const auto status = listening_name(current.ListeningStatus()) + L" | Wi-Fi status "
                        + std::to_wstring(static_cast<int>(current.WiFiStatus()));
                    post([weak, run, status] {
                        if (auto self = weak.lock(); self && self->generation.load() == run && !self->stop_requested)
                            self->set_status(status);
                    });
                }
                Sleep(100);
            }
        } catch (hresult_canceled const&) { send_log(L"Receiver operation cancelled."); }
        catch (hresult_error const& error) { final_status = L"Receiver failed: " + error_text(error); }
        catch (std::exception const& error) { final_status = L"Receiver failed: " + std::wstring(to_hstring(error.what())); }
        if (session) {
            try {
                if (have_session_events) {
                    session.ConnectionCreated(connected); session.Disconnected(disconnected); session.MediaSourceCreated(source_created);
                }
                session.Close();
            } catch (hresult_error const& error) { send_log(L"Session cleanup: " + error_text(error)); }
        }
        try { if (receiver && have_changed) receiver.StatusChanged(changed); } catch (...) {}
        session = nullptr;
        receiver = nullptr;
        auto weak = weak_from_this();
        post([weak, run, final_status] {
            if (auto self = weak.lock(); self && self->generation.load() == run) {
                self->clear_playback();
                EnableWindow(self->start_button, TRUE);
                EnableWindow(self->name_edit, TRUE);
                EnableWindow(self->stop_button, FALSE);
                self->set_status(final_status);
            }
        });
        worker_finished = true;
        if (apartment_initialized) uninit_apartment();
    }
    void begin_close() {
        if (closing.exchange(true)) return;
        stop_requested = true;
        clear_playback();
        EnableWindow(start_button, FALSE); EnableWindow(stop_button, FALSE);
        SetTimer(window, close_timer, 50, nullptr);
    }
    void finish_close() {
        if (!worker_finished || queue_shutdown_started) return;
        if (worker.joinable()) worker.join();
        queue_shutdown_started = true;
        KillTimer(window, close_timer);
        try {
            image = nullptr; root = nullptr;
            if (target) target.Root(nullptr);
            target = nullptr;
            if (compositor) compositor.Close();
            compositor = nullptr;
            if (queue) {
                const auto hwnd = window;
                queue_shutdown = queue.ShutdownQueueAsync();
                queue_shutdown.Completed([hwnd](auto const&, AsyncStatus result) { PostMessageW(hwnd, queue_closed_message, result == AsyncStatus::Completed ? 1 : 0, 0); });
                queue = nullptr;
                return;
            }
        } catch (...) {}
        DestroyWindow(window);
    }
};

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window = window;
    }
    if (app) {
        try {
            switch (message) {
            case WM_COMMAND:
                if (LOWORD(wparam) == start_id) { app->start_receiver(); return 0; }
                if (LOWORD(wparam) == stop_id) { app->stop_receiver(); return 0; }
                break;
            case WM_SIZE: app->layout(); return 0;
            case task_message: app->drain_tasks(); return 0;
            case WM_CLOSE: app->begin_close(); return 0;
            case WM_TIMER:
                if (wparam == smoke_timer) { KillTimer(window, smoke_timer); app->begin_close(); return 0; }
                if (wparam == close_timer) { app->finish_close(); return 0; }
                break;
            case queue_closed_message: app->queue_shutdown_completed = wparam != 0; DestroyWindow(window); return 0;
            case WM_DESTROY: app->window = nullptr; PostQuitMessage(0); return 0;
            }
        } catch (hresult_error const& error) { app->set_status(L"Error: " + error_text(error)); return 0; }
        catch (...) {
            if (app->options.ui_smoke) PostQuitMessage(1);
            else MessageBoxW(window, L"The Miracast window encountered an unexpected error.", L"Fucairserver", MB_ICONERROR);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int run_window(Options const& options, HINSTANCE instance, int show) {
    init_apartment(apartment_type::single_threaded);
    SetProcessDPIAware();
    auto app = std::make_shared<App>(options);
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = window_proc; type.hInstance = instance;
    type.lpszClassName = L"FucairserverIndependentMiracast";
    type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_FUCAIRSERVER)); type.hIconSm = type.hIcon;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&type)) throw_last_error();
    const auto window = CreateWindowExW(0, type.lpszClassName, L"Fucairserver - Wireless Display", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr, instance, app.get());
    if (!window) throw_last_error();
    app->initialize(window);
    ShowWindow(window, options.ui_smoke ? SW_HIDE : show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    const bool shutdown_completed = app->queue_shutdown_completed;
    if (options.ui_smoke && !options.report.empty()) {
        if (options.report.has_parent_path()) std::filesystem::create_directories(options.report.parent_path());
        std::ofstream report(options.report, std::ios::binary);
        report << "{\n  \"operation\": \"window/composition startup and orderly shutdown without receiving\",\n"
               << "  \"success\": " << (shutdown_completed ? "true" : "false") << ",\n"
               << "  \"compositionInitialized\": true,\n  \"dispatcherQueueShutdownCompleted\": " << (shutdown_completed ? "true" : "false") << ",\n"
               << "  \"receiverStarted\": false,\n  \"settingsApplied\": false\n}\n";
    }
    app.reset();
    uninit_apartment();
    return options.ui_smoke && !shutdown_completed ? 1 : static_cast<int>(message.wParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    bool probe_mode = false;
    try {
        const auto options = parse_options();
        probe_mode = options.probe || options.ui_smoke;
        return options.probe ? probe(options) : run_window(options, instance, show);
    } catch (winrt::hresult_error const& error) {
        const auto text = error_text(error);
        if (probe_mode) std::cerr << winrt::to_string(text) << '\n';
        else MessageBoxW(nullptr, text.c_str(), L"Fucairserver - startup failed", MB_ICONERROR);
    } catch (std::exception const& error) {
        if (probe_mode) std::cerr << error.what() << '\n';
        else MessageBoxA(nullptr, error.what(), "Fucairserver - startup failed", MB_ICONERROR);
    }
    return 1;
}
