#include "video/browser_video_target.h"
#include "core/logger.h"

#include <shellapi.h>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <Windows.h>

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

} // namespace

BrowserVideoTarget::BrowserVideoTarget(std::wstring url, bool fullscreen,
                                         std::wstring browser_path)
    : url_(std::move(url)),
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
    // chrome on PATH first. Then a few well-known absolute install locations
    // as fallbacks.
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
    a += L"--new-window \"" + url_ + L"\"";
    if (fullscreen_) a += L" --start-maximized";
    // No isolated profile: we reuse the user's everyday browser install.
    a += L" --no-first-run --no-default-browser-check";
    return a;
}

std::vector<std::wstring> BrowserVideoTarget::MatchKeywords() const {
    // Match by the browser's stable title suffix, NOT by the page URL. The
    // suffix ("- Google Chrome" / "- Microsoft Edge") is present on every
    // top-level window regardless of which page is loaded, so it survives the
    // async title changes (e.g. going from bilibili to douyin).
    std::wstring exe = BrowserExeName();  // already lower-cased
    std::vector<std::wstring> kw;
    if (exe.find(L"edge") != std::wstring::npos) {
        kw.push_back(L"microsoft edge");
    } else {
        // chrome / chromium / any chromium-based build
        kw.push_back(L"google chrome");
    }
    return kw;
}

HWND BrowserVideoTarget::FindTargetWindow() const {
    auto kw = MatchKeywords();
    struct Ctx { const std::vector<std::wstring>* kw; HWND found = nullptr; } ctx{&kw, nullptr};
    EnumWindows([](HWND h, LPARAM lParam) -> BOOL {
        auto* p = reinterpret_cast<Ctx*>(lParam);
        // Only real browser top-level windows carry the Chrome/Edge class.
        wchar_t cls[64]{};
        if (GetClassNameW(h, cls, 63) == 0) return TRUE;
        if (::wcscmp(cls, L"Chrome_WidgetWin_1") != 0) return TRUE;
        wchar_t buf[256]{};
        if (GetWindowTextW(h, buf, 256) == 0) return TRUE;
        std::wstring low = ToLower(std::wstring(buf));
        for (const auto& k : *p->kw) {
            if (low.find(k) != std::wstring::npos) {
                p->found = h;
                return FALSE;  // first match wins
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

bool BrowserVideoTarget::LaunchAndCapture() {
    std::wstring browser = ResolveBrowserPath();
    std::wstring args = BuildArgs();
    // Full command line: quoted exe + args. lpApplicationName is NULL so the
    // executable is parsed from the first (quoted) token; this resolves both
    // PATH-relative names (chrome.exe) and absolute paths correctly.
    std::wstring cmd = L"\"" + browser + L"\" " + args;

    CSN_LOG_INFO("Launching browser for video: " + WToN(browser) + " " + WToN(args));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
        CSN_LOG_ERROR("Failed to launch browser for video target.");
        if (error_cb_) error_cb_("无法打开浏览器，请检查浏览器路径或是否已安装。");
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // We reuse the user's everyday browser (no isolated profile), so the PID
    // from CreateProcess is unreliable when the browser was already running
    // (the launcher forwards the URL to the existing instance and exits). We
    // therefore locate the window purely by its title suffix, which is stable.
    launched_ = true;
    for (int i = 0; i < 40; ++i) {
        HWND w = FindTargetWindow();
        if (w) {
            hwnd_ = w;
            DWORD owner = 0;
            GetWindowThreadProcessId(w, &owner);
            pid_ = owner;
            CSN_LOG_INFO("Video window captured.");
            return true;
        }
        Sleep(100);
    }
    CSN_LOG_WARN("Browser launched but video window not captured yet; will retry on next show.");
    return false;
}

HWND BrowserVideoTarget::Show(HWND game_hwnd) {
    if (!launched_ || !IsWindow(hwnd_)) {
        if (!LaunchAndCapture()) {
            return nullptr;
        }
    }
    // Re-locate if the handle went stale (e.g. the user closed the window).
    if (!IsWindow(hwnd_)) {
        HWND w = FindTargetWindow();
        if (w) {
            hwnd_ = w;
            DWORD owner = 0;
            GetWindowThreadProcessId(w, &owner);
            pid_ = owner;
        }
    }
    if (!hwnd_) {
        CSN_LOG_ERROR("Video window handle invalid; cannot show.");
        return nullptr;
    }

    // The game stays in the foreground and keeps rendering (so it stays
    // capturable). We only float the single video window above it as a topmost
    // layer — never steal focus, never attach input threads (that used to flag
    // the switched-to window as "not responding" / red taskbar icon).
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    ShowWindow(hwnd_, SW_SHOW);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    // Resume playback (scans all sessions, plays the first paused one).
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
    // Pause playback (scans all sessions, pauses every playing one).
    // Run GSMTC calls on a background thread so the main loop is never blocked
    // waiting for the session manager / async operations to complete.
    std::thread([this]() {
        media_.Pause();
        media_.LogStatus("Hide");
    }).detach();

    if (!hwnd_ || !IsWindow(hwnd_)) return true;
    // Drop the topmost flag so the window falls behind the still-foreground
    // game, then minimize so it stays reachable in Alt+Tab / taskbar (a hidden
    // window would vanish from Alt+Tab entirely, which is why it could not be
    // found before).
    SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(hwnd_, SW_MINIMIZE);
    return true;
}

void BrowserVideoTarget::SetErrorCallback(std::function<void(const std::string&)> cb) {
    error_cb_ = std::move(cb);
}

} // namespace csn
