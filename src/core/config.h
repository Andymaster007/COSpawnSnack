#pragma once
#include "core/types.h"
#include <filesystem>
#include <string>
#include <vector>

namespace csn {

struct Config {
    std::string window_title_substring = "Call of Duty";
    int capture_fps = 10;
    double analysis_scale = 0.5;

    RationalRect hud_roi;
    std::vector<std::string> hud_template_paths;
    double hud_match_threshold = 0.65;

    RationalRect result_roi;
    std::vector<std::string> result_keywords = {"胜利", "失败", "VICTORY", "DEFEAT"};
    double result_confidence_threshold = 0.6;

    int hud_missing_frames_to_die = 3;
    int result_confirm_frames = 2;

    std::string video_target = "chrome_douyin";

    int focus_switch_back_delay_ms = 100;
};

bool LoadConfig(const std::filesystem::path& path, Config& out);
bool SaveConfig(const std::filesystem::path& path, const Config& cfg);

} // namespace csn
