#include "ui/webview2_controller.h"
#include "core/engine.h"
#include "core/logger.h"
#include "core/app_config.h"
#include "core/browser_finder.h"
#include "core/string_util.h"
#include "ui/resource.h"

#include <wrl.h>
#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <objbase.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <thread>
#include <filesystem>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace {
// Posted from any thread to ask the main window thread to build the native UI.
// Win32 child controls must be created on the thread that owns the parent.
static const UINT WM_APP_NATIVE_FALLBACK = WM_APP + 1;

// Format a DWORD as "0x" + hex digits (diagnostic only).
std::wstring ToHexWide(DWORD value) {
    if (value == 0) return L"0";
    wchar_t buf[16];
    int written = std::swprintf(buf, 16, L"0x%X", static_cast<unsigned>(value));
    return written > 0 ? std::wstring(buf) : std::wstring();
}
}  // namespace

namespace csn {
namespace fs = std::filesystem;

// Escape a UTF-8 JSON string so it can be embedded inside a JS single-quoted
// string literal: backslash and single-quote must be escaped. (Our JSON uses
// double quotes, which are safe inside a single-quoted JS string.)
std::wstring JsonToJsArg(const std::string& utf8json) {
    std::wstring w = Utf8ToWide(utf8json);
    std::wstring out;
    out.reserve(w.size() + 8);
    for (wchar_t c : w) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'\'') out += L"\\'";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else out += c;
    }
    return out;
}

WebUI::WebUI(HWND parent, Engine* engine, std::shared_ptr<Config> config)
    : parent_(parent), engine_(engine), config_(std::move(config)) {}

WebUI::~WebUI() = default;

void WebUI::Initialize() {
    // DIAGNOSTIC: can the loader even see a runtime?
    {
        LPWSTR ver = nullptr;
        HRESULT verHR = GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver);
        if (SUCCEEDED(verHR) && ver) {
            CSN_LOG_INFO("WebView2 available runtime: " + WideToUtf8(ver));
            CoTaskMemFree(ver);
        } else {
            CSN_LOG_WARN("GetAvailableCoreWebView2BrowserVersionString failed: 0x" +
                         std::to_string(static_cast<long>(verHR)));
        }
    }

    // User-data folder under a temp dir for now (rules out %APPDATA% path issues).
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    fs::path udf = std::wstring(tmp) + L"CODMSpawnSnackWebView2";

    // Use the default WebView2 lookup (registry-based Evergreen runtime). This
    // works on normal end-user machines that have the WebView2 runtime
    // installed. On locked-down machines where the WebView2 browser process
    // cannot start (e.g. blocked by a WDAC / code-integrity policy),
    // environment creation fails and we transparently fall back to a built-in
    // native Win32 UI (see OnEnvironmentCreated -> RequestNativeFallback). End
    // users still need to download nothing either way.
    //
    // IMPORTANT: the calling thread MUST be COM-initialized in STA mode
    // (CoInitializeEx(COINIT_APARTMENTTHREADED)), otherwise this returns
    // CO_E_NOTINITIALIZED (0x800401F0).
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, udf.wstring().c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            this, &WebUI::OnEnvironmentCreated).Get());
    CSN_LOG_INFO("CreateCoreWebView2EnvironmentWithOptions sync hr: 0x" +
                 std::to_string(static_cast<long>(hr)));
    // A synchronous failure also means WebView2 is unavailable on this machine.
    // Fall back to the native UI rather than prompting to install.
    if (FAILED(hr)) {
        RequestNativeFallback();
    }
}

HRESULT WebUI::OnEnvironmentCreated(HRESULT hr, ICoreWebView2Environment* env) {
    if (FAILED(hr) || !env) {
        // WebView2 is unavailable on this machine (typically because a
        // code-integrity / WDAC policy blocks the WebView2 browser process
        // from starting). Don't give up -- use the built-in native UI instead.
        RequestNativeFallback();
        return hr;
    }
    env_ = env;
    HRESULT h2 = env_->CreateCoreWebView2Controller(
        parent_,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            this, &WebUI::OnControllerCreated).Get());
    if (FAILED(h2)) {
        RequestNativeFallback();
    }
    return S_OK;
}

HRESULT WebUI::OnControllerCreated(HRESULT hr, ICoreWebView2Controller* controller) {
    if (FAILED(hr) || !controller) {
        RequestNativeFallback();
        return hr;
    }
    controller_ = controller;
    controller_->get_CoreWebView2(&webview_);

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webview_->get_Settings(&settings))) {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
    }

    // JS -> C++ bridge.
    webview_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            this, &WebUI::OnWebMessageReceived).Get(),
        &message_token_);

    // Once the document is ready, push the current config + status to it.
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                PostConfig();
                PostStatus(engine_->IsRunning(), engine_->IsWindowFound(), engine_->IsOcrOk());
                PostHotkeyState(hotkey_ok_);
                return S_OK;
            }).Get(),
        &nav_token_);

    RECT rc{};
    GetClientRect(parent_, &rc);
    controller_->put_Bounds(rc);

    std::wstring html = LoadHtmlResourceW();
    webview_->NavigateToString(html.c_str());

    initialized_ = true;
    return S_OK;
}

void WebUI::RequestNativeFallback() {
    // The WebView2 completion callbacks can fire on a COM worker thread, not
    // the main UI thread. Building Win32 child controls from there is unsafe
    // and fails (the parent window belongs to another thread). Marshal the
    // actual creation onto the main thread via a posted message.
    if (native_requested_) return;
    native_requested_ = true;
    if (parent_) {
        PostMessageW(parent_, WM_APP_NATIVE_FALLBACK, 0, 0);
    } else {
        // No parent yet -- run synchronously as a last resort.
        StartNativeFallback();
    }
}

void WebUI::StartNativeFallback() {
    CSN_LOG_WARN("WebView2 environment unavailable on this machine; "
                 "falling back to the built-in native Win32 UI.");
    try {
        native_ui_ = std::make_unique<NativeUI>(parent_, engine_, config_);
        if (native_ui_->Create()) {
            initialized_ = true;
            native_ui_->UpdateStatus(engine_->IsRunning());
            CSN_LOG_INFO("Native Win32 UI created successfully.");
            return;
        }
        DWORD le = GetLastError();
        CSN_LOG_ERROR("Native UI Create() returned false; last error 0x" +
                      std::to_string(static_cast<unsigned long>(le)));
    } catch (const std::exception& e) {
        CSN_LOG_ERROR(std::string("Native UI creation threw: ") + e.what());
    }
    CSN_LOG_ERROR("Native UI creation failed; cannot continue without a UI.");
    PostQuitMessage(0);
}

bool WebUI::HandleCommand(WPARAM wParam, LPARAM lParam) {
    if (native_ui_) return native_ui_->HandleCommand(wParam, lParam);
    return false;
}

HRESULT WebUI::OnWebMessageReceived(ICoreWebView2*,
                                    ICoreWebView2WebMessageReceivedEventArgs* args) {
    LPWSTR jsonW = nullptr;
    if (FAILED(args->get_WebMessageAsJson(&jsonW))) return S_OK;
    std::wstring wjson(jsonW);
    ::CoTaskMemFree(jsonW);
    HandleCommand(WideToUtf8(wjson));
    return S_OK;
}

void WebUI::HandleCommand(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        std::string cmd = j.value("cmd", std::string());
        if (cmd == "getConfig") {
            PostConfig();
        } else if (cmd == "setConfig") {
            std::string url = j.value("companion_url", engine_->GetCompanionUrl());
            std::string browser = j.value("browser", engine_->GetBrowserChoice());
            std::string path = WideToUtf8(FindBrowserExecutable(browser));
            engine_->SetCompanion(url, path);
            SaveAppConfig(*config_);
        } else if (cmd == "start") {
            engine_->Start();
        } else if (cmd == "stop") {
            engine_->Stop();
        } else if (cmd == "toggle") {
            if (engine_->IsRunning()) engine_->Stop();
            else engine_->Start();
        } else if (cmd == "testSwitch") {
            PostWebMessageSafe({{"type", "test"}, {"phase", "start"}});
            // Run the show/hide cycle off the UI thread so it doesn't block the
            // message pump (which would delay the queued web messages above).
            std::thread([this]() {
                bool ok = engine_->TestSwitch();
                PostWebMessageSafe({{"type", "test"}, {"ok", ok}});
            }).detach();
        } else if (cmd == "openUrl") {
            // External links (GitHub / personal homepage): open in a real
            // browser via the adapted-browser catalog (chrome-first fallback).
            std::string url = j.value("url", std::string());
            if (!url.empty()) {
                bool ok = OpenUrlInBrowser(Utf8ToWide(url));
                CSN_LOG_INFO("OpenUrl: " + url + " -> " +
                             std::string(ok ? "launched" : "no browser / failed"));
            }
        }
    } catch (const std::exception& e) {
        CSN_LOG_WARN("WebUI command parse error: " + std::string(e.what()));
    }
}

void WebUI::PostWebMessageSafe(const nlohmann::json& j) {
    if (!webview_ || !parent_) {
        CSN_LOG_WARN("PostWebMessageSafe skipped: webview_=" +
                     std::to_string(!!webview_) + " parent_=" +
                     std::to_string(!!parent_));
        return;
    }
    // Host -> page delivery uses ExecuteScript (not PostWebMessageAsJson +
    // a JS 'message' listener). ExecuteScript runs the JS directly in the
    // page and is far more reliable: there is no dependency on the page-side
    // event listener having been registered at the right moment. We invoke the
    // page-global dispatcher window.__host(JSON.parse('<json>')).
    std::wstring js = L"if(window.__host)window.__host(JSON.parse('" +
                      JsonToJsArg(j.dump()) + L"'));";
    std::wstring* pw = new std::wstring(std::move(js));
    if (!PostMessageW(parent_, WM_APP_WEBMSG, 0, reinterpret_cast<LPARAM>(pw))) {
        CSN_LOG_WARN("PostWebMessageSafe: PostMessage failed; discarding: " +
                     WideToUtf8(*pw));
        delete pw;
    } else {
        CSN_LOG_INFO("PostWebMessageSafe queued: " + WideToUtf8(*pw));
    }
}

void WebUI::FlushWebMessage(const std::wstring& js) {
    if (!webview_) {
        CSN_LOG_WARN("FlushWebMessage: webview_ is null; cannot deliver: " +
                     WideToUtf8(js));
        return;
    }
    // Run the JS on the page's main world. The no-op completion handler is
    // required; we don't care about the script's return value.
    HRESULT hr = webview_->ExecuteScript(
        js.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
    CSN_LOG_INFO("FlushWebMessage ExecuteScript hr=0x" +
                 std::to_string(static_cast<long>(hr)) + " js=" + WideToUtf8(js));
}

void WebUI::PostConfig() {
    if (native_ui_) return;
    PostWebMessageSafe({
        {"type", "config"},
        {"companion_url", engine_->GetCompanionUrl()},
        {"browser", engine_->GetBrowserChoice()}
    });
}

void WebUI::PostStatus(bool monitoring, bool window_found, bool ocr_ok) {
    if (native_ui_) {
        native_ui_->UpdateStatus(monitoring, ocr_ok);
        return;
    }
    PostWebMessageSafe({{"type", "status"},
                        {"monitoring", monitoring},
                        {"window_found", window_found},
                        {"ocr_ok", ocr_ok}});
}

void WebUI::PostToast(const std::string& msg) {
    if (native_ui_) return;  // native fallback logs instead of showing toasts
    PostWebMessageSafe({{"type", "toast"}, {"msg", msg}});
}

void WebUI::PostHotkeyState(bool ok) {
    PostWebMessageSafe({{"type", "hotkey"}, {"ok", ok}});
}

void WebUI::ResizeTo(RECT rc) {
    if (controller_) controller_->put_Bounds(rc);
}

std::wstring WebUI::LoadHtmlResourceW() {
    // The HTML resource is embedded as the predefined RT_HTML type (numeric 23)
    // -- see resources.rc.in, where a bare "HTML" token is compiled by RC into
    // RT_HTML. FindResourceW matches against the calling thread's UI language,
    // so on a non-English system the lookup can fail even though the resource
    // exists. We therefore also try an explicit English (0x0409) and a neutral
    // language, plus the string form "HTML" as a fallback for other builds.
    auto try_find = [](LPCWSTR type) -> HRSRC {
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCE(IDR_HTML_MAIN), type);
        if (r) return r;
        r = FindResourceExW(nullptr, type, MAKEINTRESOURCE(IDR_HTML_MAIN),
                            MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
        if (r) return r;
        r = FindResourceExW(nullptr, type, MAKEINTRESOURCE(IDR_HTML_MAIN),
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
        return r;
    };
    // Prefer RT_HTML (numeric 23); fall back to the string form.
    HRSRC hrsrc = try_find(MAKEINTRESOURCE(23) /*RT_HTML*/);
    if (!hrsrc) hrsrc = try_find(L"HTML");
    if (!hrsrc) {
        return L"<html><body>UI resource missing.</body></html>";
    }
    HGLOBAL hg = LoadResource(nullptr, hrsrc);
    if (!hg) return L"<html><body>UI resource missing.</body></html>";
    DWORD size = SizeofResource(nullptr, hrsrc);
    const char* data = static_cast<const char*>(LockResource(hg));
    std::string utf8(data, size);
    return Utf8ToWide(utf8);
}

} // namespace csn
