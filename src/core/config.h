#pragma once
#include "core/types.h"
#include <filesystem>
#include <string>
#include <vector>

namespace csn {

struct Config {
    std::string window_title_substring = "使命召唤手游";
    int capture_fps = 10;
    double analysis_scale = 0.5;

    RationalRect hud_roi{0.012, 0.954, 0.211, 0.977};
    std::vector<std::string> hud_template_paths = {
        "assets/templates/hud_bar_segments_100.png",
        "assets/templates/hud_bar_segments_75.png",
        "assets/templates/hud_bar_segments_50.png",
        "assets/templates/hud_bar_segments_25.png",
        "assets/templates/hud_bar_segments_10.png"
    };
    double hud_match_threshold = 0.65;
    // Hysteresis lower bound for the HUD-present decision. Once the HUD is
    // judged Present it stays Present until the match score drops below this
    // value. This prevents the "damage darkening" effect (low health crushes
    // the bar's contrast) from flickering the HUD to Absent while the player
    // is still alive. Set well below hud_match_threshold.
    double hud_absent_threshold = 0.35;

    // Equipment icon (e.g. "F 装备") shown when the loadout/backpack screen
    // replaces the HUD bar. Detecting it cancels the death countdown so an
    // alive player changing loadouts is not treated as death.
    RationalRect equipment_roi{0.0063, 0.9147, 0.0766, 0.9722};
    std::string equipment_template_path = "assets/templates/equipment_f_icon.png";
    double equipment_match_threshold = 0.65;

    RationalRect result_roi{0.30, 0.22, 0.70, 0.52};
    std::vector<std::string> result_keywords = {"胜利", "战败", "失败", "VICTORY", "DEFEAT"};
    double result_confidence_threshold = 0.6;
    // Before OCR, the result-text ROI is upscaled so its height reaches at
    // least this many pixels. Keeps recognition accuracy consistent across
    // device resolutions (text is tiny on low-res windows otherwise).
    int result_upscale_min_height = 360;

    int hud_missing_frames_to_die = 5;
    int result_confirm_frames = 2;
    // Consecutive HUD-present frames required before switching back to the game
    // after a death (mirrors hud_missing_frames_to_die but in the opposite
    // direction). Prevents a single-frame HUD flicker during the death cam from
    // bouncing focus back to the game and immediately re-triggering a video
    // switch.
    int hud_respawn_frames = 5;

    // After the HUD is confirmed absent (death), wait this many milliseconds
    // before actually switching to the video. This grace window lets a
    // round/match that ends right after death surface its result text (胜利/
    // 战败) first, which cancels the switch via the result branch. Without it
    // we would flip to video and then immediately back, a jarring flash.
    int death_switch_delay_ms = 3000;

    std::string video_target = "chrome_douyin";

    int focus_switch_back_delay_ms = 100;

    // When true, the program only detects (HUD + result text) and logs every
    // state change to csn-diagnose.log without switching focus or launching
    // video. Useful for verifying detection before trusting the auto-switch.
    bool diagnostic_mode = false;

    // How often to save debug ROI crops in diagnostic mode (0 = disabled).
    int diagnostic_crop_interval_seconds = 10;
};

bool LoadConfig(const std::filesystem::path& path, Config& out);
bool SaveConfig(const std::filesystem::path& path, const Config& cfg);

} // namespace csn
