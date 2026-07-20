#pragma once
#include "core/types.h"
#include <functional>

namespace csn {

class StateMachine {
public:
    enum class State {
        Idle,
        InGame,
        OnVideo,
        RoundFinished
    };

    struct Dependencies {
        std::function<void()> switch_to_video;
        std::function<void()> switch_back_to_game;
        std::function<void()> on_result_confirmed;
    };

    explicit StateMachine(const Dependencies& deps);

    void Update(const HudResult& hud, const ResultText& result);
    State GetState() const;

    void SetConfig(int hud_missing_frames, int result_confirm_frames);

private:
    Dependencies deps_;
    State state_ = State::Idle;
    bool hud_seen_ = false;
    int hud_missing_frames_ = 0;
    int hud_missing_threshold_ = 3;
    int result_confirm_frames_ = 0;
    int result_confirm_threshold_ = 2;
};

} // namespace csn
