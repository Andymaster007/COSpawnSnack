#pragma once
#include "video/ivideo_target.h"
#include <Windows.h>
#include <string>

namespace csn {

class ChromeDouyinTarget : public IVideoTarget {
public:
    bool Launch() override;
    bool Pause() override;
    bool Resume() override;
    bool IsRunning() const override;

private:
    HWND FindTargetWindow() const;
    bool SendSpaceKey(HWND hwnd);
    bool running_ = false;
};

} // namespace csn
