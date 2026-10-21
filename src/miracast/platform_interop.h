#pragma once
#include <windows.h>
#include <unknwn.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

// ABI declarations from Microsoft's windows.ui.composition.interop.h and
// DispatcherQueue.h. Kept local because MinGW's generated SDK omits these
// desktop interop declarations. Their source URLs are in THIRD_PARTY_NOTICES.md.
struct FucCompositorDesktopInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(HWND, BOOL, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnsureOnThread(DWORD) = 0;
};
inline constexpr winrt::guid compositor_desktop_interop_id{
    0x29e691fa, 0x4567, 0x4dca, {0xb3, 0x19, 0xd0, 0xf2, 0x07, 0xeb, 0x68, 0x07}};

inline winrt::Windows::System::DispatcherQueueController create_dispatcher_queue() {
    struct Options { DWORD size; int thread_type; int apartment_type; };
    static_assert(sizeof(Options) == 12);
    using CreateQueue = HRESULT(WINAPI*)(Options, void**);
    // The queue's interfaces may retain vtables in CoreMessaging, so retain
    // this one process-local module reference for the process lifetime.
    static const auto module = LoadLibraryExW(L"CoreMessaging.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) winrt::throw_last_error();
    const auto create = reinterpret_cast<CreateQueue>(GetProcAddress(module, "CreateDispatcherQueueController"));
    if (!create) winrt::throw_last_error();
    winrt::Windows::System::DispatcherQueueController controller{nullptr};
    // DQTYPE_THREAD_CURRENT requires DQTAT_COM_NONE; the caller already owns STA.
    const HRESULT result = create(Options{sizeof(Options), 2, 0}, winrt::put_abi(controller));
    winrt::check_hresult(result);
    return controller;
}

inline winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget create_window_target(
    winrt::Windows::UI::Composition::Compositor const& compositor, HWND window) {
    winrt::com_ptr<FucCompositorDesktopInterop> interop;
    winrt::check_hresult(winrt::get_unknown(compositor)->QueryInterface(compositor_desktop_interop_id, interop.put_void()));
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    winrt::check_hresult(interop->CreateDesktopWindowTarget(window, TRUE, winrt::put_abi(target)));
    return target;
}
