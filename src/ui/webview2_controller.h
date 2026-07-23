#pragma once
#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>
#include <string>
#include <memory>

#include "core/config.h"
#include "ui/native_ui.h"

namespace csn {

// Posted from any thread to ask the main window thread to deliver a C++->JS
// message. lParam is a heap-allocated std::wstring* that the handler frees.
static const UINT WM_APP_WEBMSG = WM_APP + 2;

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
    void PostStatus(bool monitoring, bool window_found, bool ocr_ok);
    // Pushes a transient message that the page shows as a toast.
    void PostToast(const std::string& msg);
    void PostConfig();
    // Records whether the global F8 hotkey was successfully registered. When it
    // failed (e.g. another app already owns F8), the page falls back to an
    // in-page F8 listener so the key still works while the window is focused.
    void SetHotkeyAvailable(bool ok) { hotkey_ok_ = ok; }
    void PostHotkeyState(bool ok);
    // Actually performs PostWebMessageAsJson; runs on the main thread only
    // (invoked from the WM_APP_WEBMSG handler in app_window.cpp).
    // Actually performs the host->page push via ICoreWebView2::ExecuteScript
    // (calls the page-global window.__host(JSON.parse(...))); runs on the main
    // thread only (invoked from the WM_APP_WEBMSG handler in app_window.cpp).
    void FlushWebMessage(const std::wstring& js);

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
    // Posts a JSON object to the page. ALWAYS marshals onto the main/UI thread
    // (via a posted window message) because ICoreWebView2::PostWebMessageAsJson
    // must run on the WebView2 owner thread and silently drops when called from
    // a worker thread or re-entrant inside a WebView2 callback. This is what
    // makes engine status / config / hotkey pushes reliably reach the page.
    void PostWebMessageSafe(const nlohmann::json& j);

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
    bool hotkey_ok_ = false;
};

} // namespace csn
