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

    void Update(const HudResult& hud, const ResultText& result,
                const EquipmentResult& equipment);
    State GetState() const;

    void SetConfig(int hud_missing_frames, int result_confirm_frames,
                   int death_switch_delay_ms, int hud_respawn_frames);

private:
    Dependencies deps_;
    State state_ = State::Idle;
    bool hud_seen_ = false;
    int hud_missing_frames_ = 0;
    int hud_missing_threshold_ = 5;
    int hud_present_frames_ = 0;
    int hud_respawn_threshold_ = 5;
    int result_confirm_frames_ = 0;
    int result_confirm_threshold_ = 2;
    // Latch: true once a result episode has been confirmed, so the
    // on_result_confirmed callback and reset fire exactly ONCE per result
    // (the 战败/胜利 banner stays on screen for seconds, which would otherwise
    // re-trigger the confirmation every frame). Re-armed (set false) when the
    // next round starts (HUD Present) and result text is gone, with a 90-frame
    // safety-net fallback for the end of a match.
    bool result_active_ = false;
    int result_absent_frames_ = 0;
    // Timestamp (steady_clock ms) at which the death-delay countdown began.
    // 0 means "not counting down".
    long long pending_death_since_ms_ = 0;
    int death_switch_delay_ms_ = 3000;
};

} // namespace csn
