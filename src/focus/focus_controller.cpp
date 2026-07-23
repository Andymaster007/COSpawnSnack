#include "focus/focus_controller.h"
#include "core/logger.h"

namespace csn {

HWND FocusController::FindWindowByTitle(const std::wstring& title_substring) {
    // First pass: exact title match. This prevents a wrapper/launcher window
    // (e.g. "使命召唤手游模拟器高清版") from being selected when the user says
    // the game window is exactly "使命召唤手游".
    {
        struct Ctx { std::wstring sub; HWND found; };
        Ctx ctx{title_substring, nullptr};
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* p = reinterpret_cast<Ctx*>(lParam);
            wchar_t buf[256]{};
            if (GetWindowTextW(hwnd, buf, 256) > 0 && IsWindowVisible(hwnd)) {
                if (std::wstring(buf) == p->sub) {
                    p->found = hwnd;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.found) return ctx.found;
    }

    // Second pass: substring, prefer the shortest title that contains the substring.
    // This picks the most specific window when several titles share the same prefix.
    {
        struct Ctx { std::wstring sub; HWND best; std::size_t best_len; };
        Ctx ctx{title_substring, nullptr, static_cast<std::size_t>(-1)};
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* p = reinterpret_cast<Ctx*>(lParam);
            wchar_t buf[256]{};
            if (GetWindowTextW(hwnd, buf, 256) > 0 && IsWindowVisible(hwnd)) {
                std::wstring title(buf);
                if (title.find(p->sub) != std::wstring::npos) {
                    if (title.length() < p->best_len) {
                        p->best_len = title.length();
                        p->best = hwnd;
                    }
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        return ctx.best;
    }
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

} // namespace csn
