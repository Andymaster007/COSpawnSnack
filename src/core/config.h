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

    RationalRect hud_roi{0.0, 0.85, 0.25, 1.0};
    std::vector<std::string> hud_template_paths = {
        "assets/templates/hud_bar_segments_full.png",
        "assets/templates/hud_bar_segments_low.png"
    };
    double hud_match_threshold = 0.65;

    RationalRect result_roi{0.30, 0.22, 0.70, 0.52};
    std::vector<std::string> result_keywords = {"胜利", "战败", "失败", "VICTORY", "DEFEAT"};
    double result_confidence_threshold = 0.6;
    // Before OCR, the result-text ROI is upscaled so its height reaches at
    // least this many pixels. Keeps recognition accuracy consistent across
    // device resolutions (text is tiny on low-res windows otherwise).
    int result_upscale_min_height = 360;

    int hud_missing_frames_to_die = 3;
    int result_confirm_frames = 2;

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
