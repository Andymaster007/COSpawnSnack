#pragma once
#include "video/ivideo_target.h"
#include "video/media_controller.h"
#include <Windows.h>
#include <string>
#include <vector>

namespace csn {

// Opens any web page (Douyin / Bilibili / Kuaishou / Xiaohongshu / a blog /
// an academic site / ...) in Chrome and manages a SINGLE window for the whole
// session:
//   - Show():  launch on first use, then maximize + bring to front + resume.
//   - Hide():  pause playback, then sink the window directly behind the game
//              window (z-order) so the game stays on top. Never closes it.
//
// Playback is controlled through the Windows Global System Media Transport
// Controls (GSMTC) so we know the REAL play/pause state and never double-toggle
// when the user pauses manually. See MediaController.
class BrowserVideoTarget : public IVideoTarget {
public:
    // url         : any web page URL, e.g. https://www.douyin.com (or a blog / academic site)
    // app_mode    : true  -> chrome --app=<url> (borderless dedicated window)
    //               false -> chrome --new-window <url> (normal browser window)
    // fullscreen  : maximize the window on show (fullscreen-window mode)
    // browser_path: explicit chrome/edge exe; empty -> "chrome.exe" on PATH
    BrowserVideoTarget(std::wstring url, bool app_mode, bool fullscreen,
                       std::wstring browser_path = {});

    HWND Show(HWND game_hwnd) override;
    bool Hide(HWND game_hwnd) override;
    bool IsRunning() const override;

private:
    std::wstring ResolveBrowserPath() const;
    std::wstring BrowserExeName() const;
    std::wstring BuildArgs() const;
    std::vector<std::wstring> MatchKeywords() const;

    HWND FindTargetWindow() const;
    bool LaunchAndCapture();
    bool ForceForeground(HWND hwnd);
    void Maximize(HWND hwnd);
    bool SendBehind(HWND hwnd, HWND game_hwnd);

    std::wstring url_;
    bool app_mode_;
    bool fullscreen_;
    std::wstring browser_path_;

    HWND hwnd_ = nullptr;
    bool launched_ = false;   // a window has been opened at least once
    MediaController media_;   // reads/controls real playback state
};

} // namespace csn
