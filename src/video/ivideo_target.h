#pragma once
#include <Windows.h>

namespace csn {

// Controls the "video window" shown while the player is dead.
//
// The new model never closes the video window: it only moves it between the
// foreground (player is dead -> watch video) and the background, sitting
// directly behind the game window (player is alive -> game on top, video
// paused). This keeps exactly ONE video window alive for the whole session.
class IVideoTarget {
public:
    // Bring the video window to the front (launch it on first use) and resume
    // playback if it was paused. Returns the video window handle, or nullptr on
    // failure.
    virtual HWND Show(HWND game_hwnd) = 0;

    // Send the video window behind the game window and pause playback. The
    // window is intentionally NOT closed. Returns true on success.
    virtual bool Hide(HWND game_hwnd) = 0;

    virtual bool IsRunning() const = 0;
    virtual ~IVideoTarget() = default;
};

} // namespace csn
