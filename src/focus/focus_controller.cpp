#include "focus/focus_controller.h"
#include "core/logger.h"

namespace csn {

namespace {

struct EnumCtx {
    std::wstring substring;
    HWND found = nullptr;
};

} // namespace

HWND FocusController::FindWindowByTitle(const std::wstring& title_substring) {
    EnumCtx ctx{title_substring};

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* p = reinterpret_cast<EnumCtx*>(lParam);
        wchar_t buf[256]{};
        if (GetWindowTextW(hwnd, buf, 256) > 0) {
            std::wstring title(buf);
            if (title.find(p->substring) != std::wstring::npos && IsWindowVisible(hwnd)) {
                p->found = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.found;
}

bool FocusController::SwitchToWindow(HWND hwnd) {
    if (!IsWindow(hwnd)) return false;

    DWORD target_thread = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD current_thread = GetCurrentThreadId();

    if (target_thread != current_thread) {
        AttachThreadInput(current_thread, target_thread, TRUE);
    }

    UINT flash = 0;
    SystemParametersInfoW(SPI_GETFOREGROUNDFLASHCOUNT, 0, &flash, 0);
    SystemParametersInfoW(SPI_SETFOREGROUNDFLASHCOUNT, 0, nullptr, SPIF_SENDCHANGE);

    bool ok = SetForegroundWindow(hwnd) != FALSE;

    SystemParametersInfoW(SPI_SETFOREGROUNDFLASHCOUNT, flash, nullptr, SPIF_SENDCHANGE);

    if (target_thread != current_thread) {
        AttachThreadInput(current_thread, target_thread, FALSE);
    }

    if (!ok) {
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        AllowSetForegroundWindow(ASFW_ANY);
        ok = SetForegroundWindow(hwnd) != FALSE;
    }

    if (!ok) {
        CSN_LOG_ERROR("Failed to set foreground window.");
    }
    return ok;
}

bool FocusController::SwitchToWindowByTitle(const std::wstring& title_substring) {
    HWND hwnd = FindWindowByTitle(title_substring);
    if (!hwnd) {
        std::string narrow(title_substring.begin(), title_substring.end());
        CSN_LOG_ERROR("Window not found: " + narrow);
        return false;
    }
    return SwitchToWindow(hwnd);
}

} // namespace csn
