#include "ui/app_window.h"
#include "core/logger.h"

#include <string>

namespace csn {

const wchar_t kClassName[] = L"CODMSpawnSnackWindow";
const int kHotkeyId = 1;

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

    if (!RegisterClassExW(&wc)) {
        CSN_LOG_ERROR("Failed to register window class.");
        return false;
    }

    hwnd_ = CreateWindowExW(
        0, kClassName, L"CODM时间管理器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 580,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_) {
        CSN_LOG_ERROR("Failed to create window.");
        return false;
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
            engine_->SetStatusCallback([this](bool monitoring) {
                if (webui_) webui_->PostStatus(monitoring);
            });
            webui_->Initialize();
            RegisterHotKey(hwnd_, kHotkeyId, 0, VK_F8);
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
        case WM_HOTKEY: {
            if (wParam == kHotkeyId) {
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
            engine_->Stop();
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

} // namespace csn
