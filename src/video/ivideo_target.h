#pragma once

namespace csn {

class IVideoTarget {
public:
    virtual bool Launch() = 0;
    virtual bool Pause() = 0;
    virtual bool Resume() = 0;
    virtual bool IsRunning() const = 0;
    virtual ~IVideoTarget() = default;
};

} // namespace csn
