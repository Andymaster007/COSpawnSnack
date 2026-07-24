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
// may exist in the same process (other tabs/pages). To pause precisely without
// resuming an already-paused tab:
//   - Pause(): scans ALL sessions of this browser and pauses every one that is
//             currently Playing (already-paused sessions are never touched).
//   - Play() : scans ALL sessions and resumes the FIRST one that is Paused;
//             if none is paused, does nothing.
// This keeps "pause" accurate; "resume" lands on the right tab only if the
// user keeps a single playable page open (or by luck).
class MediaController {
public:
    // browser_exe: e.g. "chrome.exe" or "msedge.exe" - used to pick the matching
    // media session among possibly several.
    explicit MediaController(std::wstring browser_exe);
    ~MediaController();

    // Pause every Playing session (already-paused ones untouched).
    void Pause();
    // Resume only the first Paused session; do nothing if none is paused.
    void Play();

    // Log the current playback status (so the user can verify state is read).
    void LogStatus(const char* context);

private:
    MediaController(const MediaController&) = delete;
    MediaController& operator=(const MediaController&) = delete;

    std::wstring browser_exe_; // lower-cased exe name, e.g. "chrome.exe"
};

} // namespace csn
