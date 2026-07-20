#include "state/state_machine.h"
#include "core/logger.h"

#include <chrono>
#include <string>

namespace csn {

namespace {
long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

StateMachine::StateMachine(const Dependencies& deps) : deps_(deps) {}

void StateMachine::SetConfig(int hud_missing_frames, int result_confirm_frames,
                              int death_switch_delay_ms, int hud_respawn_frames) {
    hud_missing_threshold_ = hud_missing_frames;
    result_confirm_threshold_ = result_confirm_frames;
    death_switch_delay_ms_ = death_switch_delay_ms;
    hud_respawn_threshold_ = hud_respawn_frames;
}

void StateMachine::Update(const HudResult& hud, const ResultText& result,
                          const EquipmentResult& equipment) {
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
            hud_seen_ = false;
            hud_missing_frames_ = 0;
            pending_death_since_ms_ = 0;
            if (state_ == State::OnVideo || state_ == State::InGame) {
                if (deps_.switch_back_to_game) deps_.switch_back_to_game();
            }
            if (deps_.on_result_confirmed) deps_.on_result_confirmed();
            state_ = State::Idle;
        }
        return;
    }

    // Re-arm the result latch only after a *long* sustained absence of the
    // result text (safety net). We deliberately do NOT re-arm on a short gap:
    // the 战败/胜利 banner stays on screen for seconds and the OCR reading
    // flickers (single/double-frame drops), so a brief absence would otherwise
    // re-trigger the already-handled round reset every time. The primary
    // re-arm happens in the HUD-Present branch (start of the next round).
    if (result_absent_frames_ >= 90) {
        result_active_ = false;
    }

    switch (hud.presence) {
        case HudResult::Presence::Present:
            hud_seen_ = true;
            hud_missing_frames_ = 0;
            pending_death_since_ms_ = 0;
            ++hud_present_frames_;
            // New round / respawn started: re-arm the result latch so the next
            // round/match-end banner triggers exactly ONE confirmation. Only do
            // this when the result text is actually gone, otherwise a lingering
            // banner at round start would immediately re-fire the callback.
            if (!result.found) {
                result_active_ = false;
                result_confirm_frames_ = 0;
                result_absent_frames_ = 0;
            }
            if (state_ == State::OnVideo) {
                // Require hud_respawn_threshold_ consecutive Present frames
                // before switching back, so a single-frame HUD flicker during
                // the death cam (or a transient detection glitch) cannot bounce
                // us back to the game and immediately re-trigger a video switch.
                // This mirrors the death-side debounce in the opposite direction.
                if (hud_present_frames_ >= hud_respawn_threshold_) {
                    CSN_LOG_INFO("HUD stable after respawn; switching back to game.");
                    if (deps_.switch_back_to_game) deps_.switch_back_to_game();
                    state_ = State::InGame;
                }
            } else {
                state_ = State::InGame;
            }
            break;

        case HudResult::Presence::Absent:
            hud_present_frames_ = 0;  // any gap resets the respawn debounce
            if (hud_seen_) {
                ++hud_missing_frames_;
                // If the equipment/backpack icon (e.g. "F 装备") is visible while
                // the HUD bar is gone, the player is changing loadouts and is
                // still alive. Cancel the death countdown; do NOT switch to video.
                // Once we have actually switched to video (OnVideo) we stop using
                // this rescue signal, which matches the user's requirement.
                if (state_ == State::InGame && equipment.found) {
                    CSN_LOG_INFO("Equipment icon detected during death countdown; cancelling switch (conf="
                                 + std::to_string(equipment.confidence) + ").");
                    hud_missing_frames_ = 0;
                    pending_death_since_ms_ = 0;
                    break;
                }
                // Require the initial frame quorum (anti single-frame glitch),
                // then start the death-delay countdown. Only switch to video
                // once the delay elapses AND the HUD is still absent. This way a
                // round/match that ends right after death surfaces its result
                // text first, which cancels the switch via the branch above.
                if (hud_missing_frames_ >= hud_missing_threshold_) {
                    long long now = NowMs();
                    if (pending_death_since_ms_ == 0) {
                        pending_death_since_ms_ = now;
                    }
                    if (state_ == State::InGame &&
                        (now - pending_death_since_ms_) >= death_switch_delay_ms_) {
                        CSN_LOG_INFO("HUD absent for " + std::to_string(now - pending_death_since_ms_) +
                                     "ms after seen; switching to video.");
                        if (deps_.switch_to_video) deps_.switch_to_video();
                        state_ = State::OnVideo;
                    }
                }
            } else {
                hud_missing_frames_ = 0;
                pending_death_since_ms_ = 0;
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
