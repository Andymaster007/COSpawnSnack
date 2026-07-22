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

    void Update(const RespawnText& respawn, const ResultText& result);
    State GetState() const;

    void SetConfig(int respawn_confirm_frames, int result_confirm_frames,
                   int respawn_absent_frames);

private:
    Dependencies deps_;
    State state_ = State::Idle;

    int respawn_confirm_frames_ = 0;
    int respawn_confirm_threshold_ = 5;
    int respawn_absent_frames_ = 0;
    int respawn_absent_threshold_ = 5;

    int result_confirm_frames_ = 0;
    int result_confirm_threshold_ = 2;
    // Latch: true once a result episode has been confirmed, so the
    // on_result_confirmed callback and reset fire exactly ONCE per result
    // (the 战败/胜利 banner stays on screen for seconds, which would otherwise
    // re-trigger the confirmation every frame). Re-armed (set false) when the
    // banner has been absent for a sustained period.
    bool result_active_ = false;
    int result_absent_frames_ = 0;
};

} // namespace csn
