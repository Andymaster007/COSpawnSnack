#include "video/browser_video_target.h"
#include "core/logger.h"

#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <thread>

namespace csn {
namespace fs = std::filesystem;

namespace {

bool FileExists(const std::wstring& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

std::string WToN(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// Best-effort foreground switch that works from a background process (mirrors
// FocusController::SwitchToWindow). Returns true if the window is foreground.
bool ForceForegroundImpl(HWND hwnd) {
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
    return ok;
}

} // namespace

BrowserVideoTarget::BrowserVideoTarget(std::wstring url, bool app_mode,
                                         bool fullscreen, std::wstring browser_path)
    : url_(std::move(url)),
      app_mode_(app_mode),
      fullscreen_(fullscreen),
      browser_path_(std::move(browser_path)),
      media_(BrowserExeName()) {}

std::wstring BrowserVideoTarget::BrowserExeName() const {
    std::wstring exe = browser_path_.empty() ? std::wstring(L"chrome.exe")
                                             : browser_path_;
    auto slash = exe.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exe = exe.substr(slash + 1);
    std::wstring low;
    low.reserve(exe.size());
    for (wchar_t c : exe) low.push_back(::towlower(c));
    return low;
}

std::wstring BrowserVideoTarget::ResolveBrowserPath() const {
    if (!browser_path_.empty() && FileExists(browser_path_)) {
        return browser_path_;
    }
    // chrome on PATH first (the previous build used "chrome.exe" and worked).
    // Then a few well-known absolute install locations as fallbacks.
    const std::wstring candidates[] = {
        L"chrome.exe",
        L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        L"C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
        L"msedge.exe",
        L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
    };
    for (const auto& c : candidates) {
        // Relative names (chrome.exe / msedge.exe) are trusted to resolve via PATH.
        if (c[0] != L'/' && !(c.size() > 1 && c[1] == L':')) {
            return c;
        }
        if (FileExists(c)) return c;
    }
    return L"chrome.exe";
}

std::wstring BrowserVideoTarget::BuildArgs() const {
    std::wstring a;
    // Quote the URL: bare hosts and URLs containing shell-special characters
    // (e.g. "&", spaces in query strings) must be wrapped so the command line
    // is parsed as a single argument.
    if (app_mode_) {
        a += L"--app=\"" + url_ + L"\"";
    } else {
        a += L"--new-window \"" + url_ + L"\"";
    }
    if (fullscreen_) {
        a += L" --start-maximized";
    }
    a += L" --no-first-run --no-default-browser-check";
    return a;
}

std::vector<std::wstring> BrowserVideoTarget::MatchKeywords() const {
    std::vector<std::wstring> kw;
    // Host keyword, e.g. "douyin", "bilibili", "kuaishou".
    std::wstring host = url_;
    auto pos = host.find(L"://");
    if (pos != std::wstring::npos) host = host.substr(pos + 3);
    auto slash = host.find(L'/');
    if (slash != std::wstring::npos) host = host.substr(0, slash);
    // strip leading "www."
    if (host.rfind(L"www.", 0) == 0) host = host.substr(4);
    auto dot = host.rfind(L'.');
    std::wstring base = (dot != std::wstring::npos) ? host.substr(0, dot) : host;
    kw.push_back(ToLower(base));
    // Known Chinese display names so the title-based fallback finds app windows
    // whose title is in Chinese (e.g. Douyin app window shows "抖音").
    std::wstring lower = ToLower(url_);
    if (lower.find(L"douyin") != std::wstring::npos) kw.push_back(L"抖音");
    else if (lower.find(L"bilibili") != std::wstring::npos) { kw.push_back(L"哔哩"); kw.push_back(L"bilibili"); }
    else if (lower.find(L"kuaishou") != std::wstring::npos) kw.push_back(L"快手");
    return kw;
}

HWND BrowserVideoTarget::FindTargetWindow() const {
    // 1) We already know our window handle -> reuse it (single-window guarantee).
    if (hwnd_ && IsWindow(hwnd_)) {
        return hwnd_;
    }
    // 2) Fallback: match an existing window by title keyword(s).
    auto kw = MatchKeywords();
    struct Ctx { const std::vector<std::wstring>* kw; HWND found = nullptr; } ctx{&kw, nullptr};
    EnumWindows([](HWND h, LPARAM lParam) -> BOOL {
        auto* p = reinterpret_cast<Ctx*>(lParam);
        wchar_t buf[256]{};
        if (GetWindowTextW(h, buf, 256) > 0 && IsWindowVisible(h)) {
            std::wstring title(buf);
            std::wstring low = ToLower(title);
            for (const auto& k : *p->kw) {
                if (low.find(ToLower(k)) != std::wstring::npos) {
                    p->found = h;
                    return FALSE;
                }
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

bool BrowserVideoTarget::LaunchAndCapture() {
    std::wstring browser = ResolveBrowserPath();
    std::wstring args = BuildArgs();

    CSN_LOG_INFO("Launching video window: " + WToN(browser) + " " + WToN(args));

    HINSTANCE result = ShellExecuteW(nullptr, L"open", browser.c_str(),
                                     args.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        CSN_LOG_ERROR("Failed to launch browser for video target.");
        return false;
    }

    // Chrome opens asynchronously; poll briefly to capture the new window.
    hwnd_ = nullptr;
    for (int i = 0; i < 20; ++i) {
        hwnd_ = FindTargetWindow();
        if (hwnd_) break;
        Sleep(100);
    }
    launched_ = true;
    if (!hwnd_) {
        CSN_LOG_WARN("Video window launched but handle not captured yet; will retry on next show.");
    }
    return hwnd_ != nullptr;
}

void BrowserVideoTarget::Maximize(HWND hwnd) {
    if (fullscreen_) {
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_MAXIMIZE);
    } else {
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_SHOW);
    }
}

bool BrowserVideoTarget::ForceForeground(HWND hwnd) {
    return ForceForegroundImpl(hwnd);
}

bool BrowserVideoTarget::SendBehind(HWND hwnd, HWND game_hwnd) {
    if (!game_hwnd || !IsWindow(game_hwnd)) {
        // No game handle: just push to the very bottom of the z-order.
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return true;
    }
    // Place the video window directly below the game window so the game (which
    // we then foreground) is always on top.
    SetWindowPos(hwnd, game_hwnd, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

HWND BrowserVideoTarget::Show(HWND game_hwnd) {
    if (!launched_ || !IsWindow(hwnd_)) {
        if (!LaunchAndCapture()) {
            return nullptr;
        }
    }
    if (!hwnd_) {
        hwnd_ = FindTargetWindow();
    }
    if (!hwnd_ || !IsWindow(hwnd_)) {
        CSN_LOG_ERROR("Video window handle invalid; cannot show.");
        return nullptr;
    }

    Maximize(hwnd_);
    ForceForeground(hwnd_);

    // Resume only if actually paused. MediaController reads the real GSMTC
    // status, so if the user already paused manually we won't re-toggle it back
    // to playing, and if the platform autoplays we won't pause it.
    // Run GSMTC calls on a background thread: they can block or deadlock the
    // calling thread on some systems, and we must never stall the main loop
    // (that would make the taskbar icon turn red / "not responding").
    std::thread([this]() {
        media_.Play();
        media_.LogStatus("Show");
    }).detach();
    return hwnd_;
}

bool BrowserVideoTarget::Hide(HWND game_hwnd) {
    if (!hwnd_ || !IsWindow(hwnd_)) {
        // Nothing to hide.
        return true;
    }

    // Pause only if actually playing (reads real status via GSMTC).
    // Run GSMTC calls on a background thread so the main loop is never blocked
    // waiting for the session manager / async operations to complete.
    std::thread([this]() {
        media_.Pause();
        media_.LogStatus("Hide");
    }).detach();

    // Sink behind the game window; do NOT close.
    SendBehind(hwnd_, game_hwnd);
    return true;
}

bool BrowserVideoTarget::IsRunning() const {
    return launched_ && hwnd_ != nullptr && IsWindow(hwnd_);
}

} // namespace csn
