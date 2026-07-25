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

// Controls playback of the companion browser via the Windows System Media
// Transport Controls (GSMTC).
//
// Broadcasting model:
//   - Pause(): pause EVERY media session owned by the companion browser.
//   - Play() : resume EVERY media session owned by the companion browser.
// Both act on ALL matching sessions, so we never need to guess "which video
// the user is watching". We do NOT read playback state before acting: an
// already-paused session is a no-op for pause, an already-playing one a no-op
// for play, so this never fights with the user's own manual pause/play.
class MediaController {
public:
    // browser_exe: e.g. "chrome.exe" or "msedge.exe" - used to match the
    //               browser's media sessions by app id.
    explicit MediaController(std::wstring browser_exe);
    ~MediaController();

    // Pause every matching media session (idempotent; safe on already-paused).
    void Pause();
    // Resume every matching media session (idempotent; safe on already-playing).
    void Play();

    // Log the current media-session state (so the user can verify control).
    void LogStatus(const char* context);

private:
    MediaController(const MediaController&) = delete;
    MediaController& operator=(const MediaController&) = delete;

    void GsmtcPlay();
    void GsmtcPause();
    void GsmtcLogStatus();

    std::wstring browser_exe_; // lower-cased exe name, e.g. "chrome.exe"
};

} // namespace csn
