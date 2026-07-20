#include "state/state_machine.h"
#include "core/logger.h"

namespace csn {

StateMachine::StateMachine(const Dependencies& deps) : deps_(deps) {}

void StateMachine::SetConfig(int hud_missing_frames, int result_confirm_frames) {
    hud_missing_threshold_ = hud_missing_frames;
    result_confirm_threshold_ = result_confirm_frames;
}

void StateMachine::Update(const HudResult& hud, const ResultText& result) {
    if (result.found) {
        ++result_confirm_frames_;
    } else {
        result_confirm_frames_ = 0;
    }

    if (result_confirm_frames_ >= result_confirm_threshold_) {
        CSN_LOG_INFO("Result text confirmed; resetting round state.");
        hud_seen_ = false;
        hud_missing_frames_ = 0;
        if (state_ == State::OnVideo || state_ == State::InGame) {
            if (deps_.switch_back_to_game) deps_.switch_back_to_game();
        }
        if (deps_.on_result_confirmed) deps_.on_result_confirmed();
        state_ = State::Idle;
        return;
    }

    switch (hud.presence) {
        case HudResult::Presence::Present:
            hud_seen_ = true;
            hud_missing_frames_ = 0;
            if (state_ == State::OnVideo) {
                CSN_LOG_INFO("HUD reappeared while on video; switching back to game.");
                if (deps_.switch_back_to_game) deps_.switch_back_to_game();
                state_ = State::InGame;
            } else {
                state_ = State::InGame;
            }
            break;

        case HudResult::Presence::Absent:
            if (hud_seen_) {
                ++hud_missing_frames_;
                if (hud_missing_frames_ >= hud_missing_threshold_ && state_ == State::InGame) {
                    CSN_LOG_INFO("HUD disappeared after being seen; switching to video.");
                    if (deps_.switch_to_video) deps_.switch_to_video();
                    state_ = State::OnVideo;
                }
            } else {
                hud_missing_frames_ = 0;
                state_ = State::Idle;
            }
            break;

        case HudResult::Presence::Unknown:
        default:
            break;
    }
}

StateMachine::State StateMachine::GetState() const {
    return state_;
}

} // namespace csn
