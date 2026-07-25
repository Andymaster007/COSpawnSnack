#pragma once
#include "video/ivideo_target.h"
#include "video/media_controller.h"
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

namespace csn {

// Opens the configured web page (Douyin / Bilibili / Kuaishou / a blog / ...) in
// the USER'S EVERYDAY browser (no isolated profile) and shows/hides THAT window
// when switching:
//   - Show(): launch on first use (opens the configured URL ONCE), then show
//              + float above the game as a topmost layer + resume playback.
//   - Hide():  pause playback, then minimize the window (stays in Alt+Tab).
//              Never closes it.
//
// Window location: we locate the browser window by its stable title suffix
// ("- Google Chrome" / "- Microsoft Edge"), which is independent of the page
// being viewed. We lock the first matching top-level window (class
// Chrome_WidgetWin_1) and keep operating on it; tabs opened inside the same
// window ride along automatically. Because we reuse the user's real browser,
// the caller MUST ensure no OTHER playable page is open in that browser during
// a session, otherwise playback pause/resume may affect the wrong tab.
class BrowserVideoTarget : public IVideoTarget {
public:
    // url         : any web page URL, e.g. https://www.douyin.com
    // fullscreen  : maximize the window on show (fullscreen-window mode)
    // browser_path: explicit chrome/edge exe; empty -> "chrome.exe" on PATH
    // cdp_port    : remote-debugging port the browser is launched with (CDP)
    // video_host  : optional URL-substring filter for CDP play/pause control
    BrowserVideoTarget(std::wstring url, bool fullscreen,
                       std::wstring browser_path = {},
                       int cdp_port = 9222,
                       std::string video_host = "");

    HWND Show(HWND game_hwnd) override;
    bool Hide(HWND game_hwnd) override;

    // Optional callback invoked when the browser cannot be launched (e.g. the
    // configured browser is missing). The Engine forwards it to the UI as a toast.
    void SetErrorCallback(std::function<void(const std::string&)> cb) override;

private:
    std::wstring ResolveBrowserPath() const;
    std::wstring BrowserExeName() const;
    std::wstring BuildArgs() const;
    std::vector<std::wstring> MatchKeywords() const;

    HWND FindTargetWindow() const;
    bool LaunchAndCapture();

    std::wstring url_;
    bool fullscreen_;
    std::wstring browser_path_;
    int cdp_port_ = 9222;
    std::string video_host_;

    DWORD pid_ = 0;                    // owning process of the locked window
    HWND hwnd_ = nullptr;              // the single browser window we manage
    bool launched_ = false;            // a window has been opened at least once
    MediaController media_;            // reads/controls real playback state
    std::function<void(const std::string&)> error_cb_;  // launch-failure reporter
};

} // namespace csn
