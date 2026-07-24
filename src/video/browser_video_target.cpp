#include "video/browser_video_target.h"
#include "core/logger.h"

#include <shellapi.h>
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

std::wstring BrowserVideoTarget::ProfileDir() {
    // Fixed, isolated profile: never touches the user's real browser, and
    // login state persists across runs. The fixed path also guarantees our
    // --user-data-dir is a distinct Chrome singleton, so the PID we capture is
    // always our own process (not an already-running instance).
    return (fs::temp_directory_path() / L"COSpawnSnackCompanionProfile").wstring();
}

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
    a += L"--new-window \"" + url_ + L"\"";
    if (fullscreen_) a += L" --start-maximized";
    // Isolated profile: distinct from the user's real browser, and persistent.
    a += L" --user-data-dir=\"" + ProfileDir() + L"\"";
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

std::vector<HWND> BrowserVideoTarget::EnumBrowserWindows() const {
    std::vector<HWND> out;
    if (!pid_) return out;
    struct Ctx { DWORD pid; std::vector<HWND>* out; } ctx{pid_, &out};
    EnumWindows([](HWND h, LPARAM lParam) -> BOOL {
        auto* p = reinterpret_cast<Ctx*>(lParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        // Genuine top-level windows only (no owner), belonging to our PID.
        if (pid == p->pid && GetWindow(h, GW_OWNER) == nullptr) {
            p->out->push_back(h);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return out;
}

HWND BrowserVideoTarget::FindTargetWindow() const {
    auto kw = MatchKeywords();
    struct Ctx { const std::vector<std::wstring>* kw; HWND found = nullptr; } ctx{&kw, nullptr};
    EnumWindows([](HWND h, LPARAM lParam) -> BOOL {
        auto* p = reinterpret_cast<Ctx*>(lParam);
        wchar_t buf[256]{};
        if (GetWindowTextW(h, buf, 256) > 0 && IsWindowVisible(h)) {
            std::wstring low = ToLower(std::wstring(buf));
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
    // Full command line: quoted exe + args. lpApplicationName is NULL so the
    // executable is parsed from the first (quoted) token; this resolves both
    // PATH-relative names (chrome.exe) and absolute paths correctly.
    std::wstring cmd = L"\"" + browser + L"\" " + args;

    CSN_LOG_INFO("Launching companion browser: " + WToN(browser) + " " + WToN(args));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
        CSN_LOG_ERROR("Failed to launch browser for video target.");
        if (error_cb_) error_cb_("无法打开浏览器，请检查浏览器路径或是否已安装。");
        return false;
    }
    pid_ = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    known_windows_.clear();
    hwnd_ = nullptr;
    for (int i = 0; i < 40; ++i) {
        auto mine = EnumBrowserWindows();
        if (!mine.empty()) {
            known_windows_ = mine;
            hwnd_ = mine.front();
            break;
        }
        // Fallback: a window matching our keywords may belong to a different PID
        // if Chrome's singleton handed the URL to an already-running instance.
        HWND fallback = FindTargetWindow();
        if (fallback) {
            DWORD owner = 0;
            GetWindowThreadProcessId(fallback, &owner);
            if (owner) pid_ = owner;
            known_windows_ = {fallback};
            hwnd_ = fallback;
            break;
        }
        Sleep(100);
    }
    launched_ = true;
    if (known_windows_.empty())
        CSN_LOG_WARN("Companion browser launched but window not captured yet; will retry on next show.");
    return !known_windows_.empty();
}

bool BrowserVideoTarget::ForceForeground(HWND hwnd) {
    return ForceForegroundImpl(hwnd);
}

HWND BrowserVideoTarget::Show(HWND game_hwnd) {
    if (!launched_ || pid_ == 0 || !IsWindow(hwnd_)) {
        if (!LaunchAndCapture()) {
            return nullptr;
        }
    }
    // Re-enumerate: the site/user may have opened more windows since we last
    // captured. We manage the WHOLE process, not a single window.
    auto wins = EnumBrowserWindows();
    if (wins.empty()) wins = known_windows_;
    if (wins.empty()) {
        HWND f = FindTargetWindow();
        if (f) {
            DWORD owner = 0;
            GetWindowThreadProcessId(f, &owner);
            if (owner) pid_ = owner;
            wins = {f};
        }
    }
    if (wins.empty()) {
        CSN_LOG_ERROR("Companion window handle invalid; cannot show.");
        return nullptr;
    }
    known_windows_ = wins;
    hwnd_ = wins.front();

    for (HWND h : wins) {
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        ShowWindow(h, SW_SHOW);
    }
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
    // Pause only if actually playing (reads real status via GSMTC).
    // Run GSMTC calls on a background thread so the main loop is never blocked
    // waiting for the session manager / async operations to complete.
    std::thread([this]() {
        media_.Pause();
        media_.LogStatus("Hide");
    }).detach();

    // Hide every window the process owns (incl. any the site spawned). Re-enumerate
    // to stay current; fall back to the last known set if enumeration is empty.
    auto wins = EnumBrowserWindows();
    if (wins.empty()) wins = known_windows_;
    known_windows_ = wins;
    for (HWND h : wins) {
        if (h && IsWindow(h)) ShowWindow(h, SW_HIDE);
    }
    return true;
}

void BrowserVideoTarget::SetErrorCallback(std::function<void(const std::string&)> cb) {
    error_cb_ = std::move(cb);
}

} // namespace csn
