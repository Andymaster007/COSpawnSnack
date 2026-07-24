#include "state/state_machine.h"
#include "core/logger.h"

#include <string>

namespace csn {

StateMachine::StateMachine(const Dependencies& deps) : deps_(deps) {}

void StateMachine::SetConfig(int respawn_confirm_frames, int result_confirm_frames) {
    respawn_confirm_threshold_ = respawn_confirm_frames;
    result_confirm_threshold_ = result_confirm_frames;
}

void StateMachine::Update(const RespawnText& respawn, const ResultText& result) {
    // ---- Round / match end detection (胜利/战败 etc.) ----
    // This is the ONLY trigger that switches back to the game. A sustained
    // absence of the respawn hint is intentionally NOT used to switch back,
    // because in-round banners (e.g. a planted bomb) can blank the respawn-hint
    // area and cause false switch-backs. The player therefore stays on video
    // until the round / match actually ends.
    if (result.found) {
        ++result_confirm_frames_;
        result_absent_frames_ = 0;
    } else {
        result_confirm_frames_ = 0;
        ++result_absent_frames_;
    }

    if (result_confirm_frames_ >= result_confirm_threshold_) {
        if (!result_active_) {
            result_active_ = true;
            CSN_LOG_INFO("Result text confirmed; resetting round state.");
            respawn_confirm_frames_ = 0;
            if (state_ == State::OnVideo || state_ == State::InGame) {
                if (deps_.switch_back_to_game) deps_.switch_back_to_game();
            }
            if (deps_.on_result_confirmed) deps_.on_result_confirmed();
            state_ = State::Idle;
        }
        return;
    }

    // Re-arm the result latch only after a long sustained absence of the
    // result text (safety net). We deliberately do NOT re-arm on a short gap:
    // the 战败/胜利 banner stays on screen for seconds and the OCR reading
    // flickers (single/double-frame drops), so a brief absence would otherwise
    // re-trigger the already-handled round reset every time.
    if (result_absent_frames_ >= 90) {
        result_active_ = false;
    }

    // ---- Respawn hint detection ("你将在下一回合重生") ----
    // Respawn hint visible for N consecutive frames -> player is dead -> video.
    if (respawn.found) {
        ++respawn_confirm_frames_;
    } else {
        respawn_confirm_frames_ = 0;
    }

    if (respawn_confirm_frames_ >= respawn_confirm_threshold_) {
        if (state_ != State::OnVideo) {
            CSN_LOG_INFO("Respawn text confirmed; switching to video.");
            if (deps_.switch_to_video) deps_.switch_to_video();
        }
        state_ = State::OnVideo;
        return;
    }
}

StateMachine::State StateMachine::GetState() const {
    return state_;
}

} // namespace csn
