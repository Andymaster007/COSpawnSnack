#include "state/state_machine.h"
#include "core/logger.h"

#include <string>

namespace csn {

StateMachine::StateMachine(const Dependencies& deps) : deps_(deps) {}

void StateMachine::SetConfig(int respawn_confirm_frames, int result_confirm_frames,
                              int respawn_absent_frames) {
    respawn_confirm_threshold_ = respawn_confirm_frames;
    result_confirm_threshold_ = result_confirm_frames;
    respawn_absent_threshold_ = respawn_absent_frames;
}

void StateMachine::Update(const RespawnText& respawn, const ResultText& result) {
    // ---- Round / match end detection (胜利/战败 etc.) ----
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
            // Round over: the respawn hint is gone and the next round will start
            // fresh, so clear any pending respawn state.
            respawn_confirm_frames_ = 0;
            respawn_absent_frames_ = 0;
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
    // A noisy in-round banner (e.g. "炸弹已被安装" / "炸弹已被拆除") may replace
    // the respawn hint in the same screen area for a few seconds. Such frames
    // are marked ignored by the caller (main.cpp) and must NOT touch the
    // respawn counters or the state at all -- the current state is preserved
    // (stay in-game if we were in-game, stay on-video if we were on-video).
    if (respawn.ignored) {
        return;
    }

    if (respawn.found) {
        ++respawn_confirm_frames_;
        respawn_absent_frames_ = 0;
    } else {
        respawn_confirm_frames_ = 0;
        ++respawn_absent_frames_;
    }

    // Respawn hint visible for N consecutive frames -> player is dead -> video.
    if (respawn_confirm_frames_ >= respawn_confirm_threshold_) {
        if (state_ != State::OnVideo) {
            CSN_LOG_INFO("Respawn text confirmed; switching to video.");
            if (deps_.switch_to_video) deps_.switch_to_video();
        }
        state_ = State::OnVideo;
        return;
    }

    // Respawn hint GONE for a sustained period -> the player is alive again
    // (respawned next round, or the round/match ended without a readable
    // 胜利/战败 banner). Switch back to the game.
    //
    // We require a LONG sustained absence (respawn_absent_threshold_ frames)
    // so a single-frame OCR flicker does not bounce focus. Short-lived in-game
    // banners are already filtered out upstream (ignored frames never reach
    // this point), so only a real, sustained disappearance (actual respawn /
    // round end) reaches this threshold.
    if (respawn_absent_frames_ >= respawn_absent_threshold_) {
        if (state_ == State::OnVideo) {
            CSN_LOG_INFO("Respawn text gone (sustained); switching back to game.");
            if (deps_.switch_back_to_game) deps_.switch_back_to_game();
        }
        respawn_confirm_frames_ = 0;
        state_ = State::InGame;
    }
}

StateMachine::State StateMachine::GetState() const {
    return state_;
}

} // namespace csn
