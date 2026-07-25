#pragma once
#include <string>

namespace csn {

// Coarse playback status we care about (maps from the WinRT GSMTC enum so the
// header stays free of WinRT includes).
enum class PlaybackStatus {
    Unknown,
    Playing,
    Paused,
    Stopped,
    Other
};

// Controls playback of the companion browser via the Chrome DevTools Protocol
// (CDP) over the browser's remote-debugging port, with a GSMTC fallback.
//
// Why CDP instead of (only) GSMTC: GSMTC can only act on media sessions that
// Chromium registers with Windows. A tab that is in the BACKGROUND and/or was
// paused via the page's own controls is frequently NOT registered, so
// "pause/play everything" becomes "pause/play nothing" (the
// "no media session found" we saw in the logs). CDP talks to the browser
// directly and sees EVERY tab (background, paused, live) — so broadcasting
// play/pause actually reaches them.
//
// Broadcasting model (same as before, now reliable):
//   - Pause(): for every page tab (optionally filtered by video_host), inject
//              video.pause() — idempotent (already-paused = no-op).
//   - Play() : for every page tab (optionally filtered), inject video.play()
//              — idempotent (already-playing = no-op).
// Both act on ALL matching tabs, so we never need to guess "which video the
// user is watching". If the CDP port is not reachable (e.g. the everyday
// browser was already open without remote debugging), we fall back to GSMTC.
class MediaController {
public:
    // browser_exe: e.g. "chrome.exe" or "msedge.exe" - used only for the GSMTC
    //              fallback session matching.
    // cdp_port   : remote-debugging port the browser is launched with.
    // video_host : if non-empty, only tabs whose URL contains this substring
    //              are controlled; empty = every page tab.
    explicit MediaController(std::wstring browser_exe,
                            int cdp_port = 9222,
                            std::string video_host = "");
    ~MediaController();

    // Pause every matching tab directly (idempotent; safe on already-paused).
    void Pause();
    // Resume every matching tab directly (idempotent; safe on already-playing).
    void Play();

    // Log the current control-channel state (so the user can verify CDP vs GSMTC).
    void LogStatus(const char* context);

private:
    MediaController(const MediaController&) = delete;
    MediaController& operator=(const MediaController&) = delete;

    // CDP broadcast. Returns true if CDP was reachable (even if zero tabs
    // matched) so the caller skips the GSMTC fallback; false if CDP was not
    // available and the caller should fall back.
    bool CdpBroadcast(bool play);

    // GSMTC fallback paths (only used when CDP is unreachable).
    void GsmtcPlay();
    void GsmtcPause();
    void GsmtcLogStatus();

    std::wstring browser_exe_; // lower-cased exe name, e.g. "chrome.exe"
    int cdp_port_ = 9222;
    std::string video_host_;   // optional URL substring filter (empty = all)
};

} // namespace csn
