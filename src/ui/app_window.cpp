#include "ui/app_window.h"
#include "core/logger.h"
#include "ui/resource.h"
#include "ui/webview2_controller.h"

#include <string>

namespace csn {

const wchar_t kClassName[] = L"CODMSpawnSnackWindow";
const int kHotkeyId = 1;     // F8
const int kHotkeyId2 = 2;    // Ctrl+F8 (fallback if F8 is occupied)

AppWindow::AppWindow(std::shared_ptr<Engine> engine, std::shared_ptr<Config> config)
    : engine_(std::move(engine)), config_(std::move(config)) {}

AppWindow::~AppWindow() = default;

bool AppWindow::Create() {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = AppWindow::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    // Load the embedded CO icon for the title bar, taskbar button and Alt+Tab.
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON));

    if (!RegisterClassExW(&wc)) {
        CSN_LOG_ERROR("Failed to register window class.");
        return false;
    }

    hwnd_ = CreateWindowExW(
        0, kClassName, L"CO摸鱼管理器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 580,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_) {
        CSN_LOG_ERROR("Failed to create window.");
        return false;
    }

    // Explicitly set both the big icon (taskbar / Alt+Tab) and the small icon
    // (title bar). This covers cases where the window-class icon is ignored,
    // e.g. by some taskbar configurations or high-DPI scaling.
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON));
    if (hIcon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void AppWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        // Make the window handle available to HandleMessage during WM_CREATE
        // (it is not yet stored in hwnd_, which is assigned after
        // CreateWindowExW returns). WebView2 needs a valid parent here.
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            webui_ = std::make_unique<WebUI>(hwnd_, engine_.get(), config_);
            // Engine status changes are pushed to the active UI (WebView2 page,
            // or the native fallback if WebView2 is unavailable).
            engine_->SetStatusCallback([this](bool monitoring, bool window_found) {
                if (webui_) webui_->PostStatus(monitoring, window_found);
            });
            webui_->Initialize();
            // Register a global F8 hotkey, plus Ctrl+F8 as a fallback for when
            // F8 is already owned by another program (overlays, Afterburner,
            // Discord, etc.). Either one toggles monitoring. If BOTH fail we
            // log it and let the UI fall back to an in-page listener (only
            // active while our window is focused). The UI button always works.
            BOOL hotkeyOk = RegisterHotKey(hwnd_, kHotkeyId, 0, VK_F8);
            BOOL hotkeyCtrlOk = RegisterHotKey(hwnd_, kHotkeyId2, MOD_CONTROL, VK_F8);
            if (!hotkeyOk && !hotkeyCtrlOk) {
                CSN_LOG_WARN("RegisterHotKey(F8 / Ctrl+F8) failed; global hotkey "
                             "unavailable. UI button still works; in-page F8 "
                             "fallback enabled when focused.");
            } else {
                CSN_LOG_INFO("Global hotkey registered: F8=" + std::to_string(hotkeyOk) +
                             " Ctrl+F8=" + std::to_string(hotkeyCtrlOk));
            }
            webui_->SetHotkeyAvailable(hotkeyOk != 0 || hotkeyCtrlOk != 0);
            return 0;
        }
        case WM_SIZE: {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            if (webui_) webui_->ResizeTo(rc);
            return 0;
        }
        case WM_COMMAND: {
            // Route control notifications (native fallback buttons/combo) to
            // the active UI. WebView2 handles its own messages internally.
            if (webui_ && webui_->HandleCommand(wParam, lParam)) return 0;
            return DefWindowProcW(hwnd_, msg, wParam, lParam);
        }
        case WM_APP + 1: {
            // WebView2 failed on a background thread; build the native UI here
            // on the main thread where the parent window lives.
            if (webui_) webui_->StartNativeFallback();
            return 0;
        }
        case WM_APP_WEBMSG: {
            // A C++->JS message (status/config/hotkey/test) was queued from
            // another thread; deliver it here on the main thread so
            // PostWebMessageAsJson runs on the WebView2 owner thread.
            auto* pw = reinterpret_cast<std::wstring*>(lParam);
            if (pw) {
                if (webui_) webui_->FlushWebMessage(*pw);
                delete pw;
            }
            return 0;
        }
        case WM_HOTKEY: {
            if (wParam == kHotkeyId || wParam == kHotkeyId2) {
                if (engine_->IsRunning()) {
                    engine_->Stop();
                } else {
                    engine_->Start();
                }
            }
            return 0;
        }
        case WM_DESTROY: {
            UnregisterHotKey(hwnd_, kHotkeyId);
            UnregisterHotKey(hwnd_, kHotkeyId2);
            engine_->Stop();
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

} // namespace csn
