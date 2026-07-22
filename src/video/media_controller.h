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
// Why this exists: the only media keyboard key (VK_MEDIA_PLAY_PAUSE) is a
// *toggle*. If we blindly pressed it on every switch we would double-toggle
// whenever the user manually paused/resumed the video. GSMTC lets us query the
// actual status and issue precise Play/Pause commands, so:
//   - switch to video  -> Play()  (only if not already playing)
//   - switch back game  -> Pause() (only if currently playing)
// Manual user actions are reflected in the reported status, so we never fight
// the user's own play/pause.
class MediaController {
public:
    // browser_exe: e.g. "chrome.exe" or "msedge.exe" - used to pick the matching
    // media session among possibly several.
    explicit MediaController(std::wstring browser_exe);
    ~MediaController();

    // True if a matching media session currently reports Playing.
    bool IsPlaying();

    // Pause only if currently playing.
    void Pause();
    // Resume/start only if not currently playing.
    void Play();

    // Log the current playback status (so the user can verify state is read).
    void LogStatus(const char* context);

private:
    MediaController(const MediaController&) = delete;
    MediaController& operator=(const MediaController&) = delete;

    std::wstring browser_exe_; // lower-cased exe name, e.g. "chrome.exe"
};

} // namespace csn
