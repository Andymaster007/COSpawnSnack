#pragma once
#include "video/ivideo_target.h"
#include "video/media_controller.h"
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

namespace csn {

// Opens any web page (Douyin / Bilibili / Kuaishou / Xiaohongshu / a blog /
// an academic site / ...) in a SEPARATE, isolated browser process and manages
// that whole process for the session:
//   - Show():  launch on first use (opens the configured URL ONCE), then show
//              + bring to front + resume ALL top-level windows of the process.
//   - Hide():  pause playback, then hide ALL top-level windows of the process.
//              Never closes it.
//
// Why per-process (not per-window): video sites (Bilibili / Kuaishou) often
// spawn their OWN additional top-level windows for playback. By tracking the
// browser PROCESS (PID) and toggling every window it owns, we stay robust no
// matter how many windows the site or the user opens. The browser manages its
// own internal layout; we only show/hide the whole instance.
//
// The browser runs under a FIXED, isolated --user-data-dir so it never touches
// the user's real Chrome/Edge, and the profile persists login state across runs.
class BrowserVideoTarget : public IVideoTarget {
public:
    // url         : any web page URL, e.g. https://www.douyin.com
    // fullscreen  : maximize the window on show (fullscreen-window mode)
    // browser_path: explicit chrome/edge exe; empty -> "chrome.exe" on PATH
    BrowserVideoTarget(std::wstring url, bool fullscreen,
                       std::wstring browser_path = {});

    HWND Show(HWND game_hwnd) override;
    bool Hide(HWND game_hwnd) override;

    // Optional callback invoked when the browser cannot be launched (e.g. the
    // configured browser is missing). The Engine forwards it to the UI as a toast.
    void SetErrorCallback(std::function<void(const std::string&)> cb) override;

private:
    static std::wstring ProfileDir();
    std::wstring ResolveBrowserPath() const;
    std::wstring BrowserExeName() const;
    std::wstring BuildArgs() const;
    std::vector<std::wstring> MatchKeywords() const;

    std::vector<HWND> EnumBrowserWindows() const;
    HWND FindTargetWindow() const;
    bool LaunchAndCapture();
    bool ForceForeground(HWND hwnd);

    std::wstring url_;
    bool fullscreen_;
    std::wstring browser_path_;

    DWORD pid_ = 0;                    // browser process we launched
    HWND hwnd_ = nullptr;              // a representative window (for foreground)
    std::vector<HWND> known_windows_;  // all top-level windows of pid_
    bool launched_ = false;            // a window has been opened at least once
    MediaController media_;            // reads/controls real playback state
    std::function<void(const std::string&)> error_cb_;  // launch-failure reporter
};

} // namespace csn
