#pragma once
#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>
#include <string>
#include <memory>

#include "core/config.h"
#include "ui/native_ui.h"

namespace csn {

class Engine;

// Hosts the WebView2 control inside the given parent window and bridges the
// HTML/JS UI to the C++ Engine via window.chrome.webview.postMessage (JS->C++)
// and ICoreWebView2::PostWebMessageAsJson (C++->JS). When WebView2 cannot be
// created on a machine (e.g. a policy blocks the WebView2 browser process),
// it transparently falls back to a built-in native Win32 UI (NativeUI) that
// exposes the same controls and talks to the same Engine methods.
class WebUI {
public:
    WebUI(HWND parent, Engine* engine, std::shared_ptr<Config> config);
    ~WebUI();

    // Kicks off async WebView2 initialization. The UI appears once the document
    // has loaded (NavigationCompleted).
    void Initialize();
    void ResizeTo(RECT rc);
    // Push current state to the page (called on init and on engine status change).
    void PostStatus(bool monitoring);
    void PostConfig();

    // Forwards WM_COMMAND from the host window to the native fallback (only
    // relevant when WebView2 is unavailable). Returns true if handled.
    bool HandleCommand(WPARAM wParam, LPARAM lParam);

    // Activated when WebView2 cannot be created (e.g. blocked by policy).
    // Safe to call from any thread: it marshals the actual UI creation onto
    // the main window thread via a posted message, because Win32 child
    // controls MUST be created on the same thread that owns the parent window.
    void RequestNativeFallback();
    // Actually builds the native UI. Must run on the main thread (invoked from
    // the WM_APP_NATIVE_FALLBACK message handler in app_window.cpp).
    void StartNativeFallback();

private:
    HRESULT OnEnvironmentCreated(HRESULT hr, ICoreWebView2Environment* env);
    HRESULT OnControllerCreated(HRESULT hr, ICoreWebView2Controller* controller);
    HRESULT OnWebMessageReceived(ICoreWebView2* sender,
                                 ICoreWebView2WebMessageReceivedEventArgs* args);
    void HandleCommand(const std::string& json);
    std::wstring LoadHtmlResourceW();

    HWND parent_ = nullptr;
    Engine* engine_ = nullptr;
    std::shared_ptr<Config> config_;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;

    // Active when WebView2 is unavailable; mirrors the HTML UI with native
    // Win32 controls so the program still works on locked-down machines.
    std::unique_ptr<NativeUI> native_ui_;

    EventRegistrationToken message_token_{};
    EventRegistrationToken nav_token_{};
    bool initialized_ = false;
    bool native_requested_ = false;
};

} // namespace csn
