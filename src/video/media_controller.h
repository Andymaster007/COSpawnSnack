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

// Reads and controls the REAL playback state of the video browser via the
// Windows Global System Media Transport Controls (GSMTC) session manager.
//
// Because the browser is the user's everyday install, several media sessions
// may exist in the same process (other tabs/pages). To control playback
// without depending on the (sometimes stale/inaccurate) GSMTC status report:
//   - Pause(): scans ALL sessions of this browser and sends TryPauseAsync to
//              every one directly (idempotent; already-paused = no-op).
//   - Play() : scans ALL sessions and sends TryPlayAsync to every one directly
//              (idempotent; already-playing = no-op).
// Both act on ALL media of this browser (the intended scope). This avoids
// skipping a session whose GSMTC status is stale after the user pauses/plays
// via the page's own controls.
class MediaController {
public:
    // browser_exe: e.g. "chrome.exe" or "msedge.exe" - used to pick the matching
    // media session among possibly several.
    explicit MediaController(std::wstring browser_exe);
    ~MediaController();

    // Pause every matching session directly (idempotent; safe on already-paused).
    void Pause();
    // Resume every matching session directly (idempotent; safe on already-playing).
    void Play();

    // Log the current playback status (so the user can verify state is read).
    void LogStatus(const char* context);

private:
    MediaController(const MediaController&) = delete;
    MediaController& operator=(const MediaController&) = delete;

    std::wstring browser_exe_; // lower-cased exe name, e.g. "chrome.exe"
};

} // namespace csn
