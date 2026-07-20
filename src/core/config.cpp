#include "core/config.h"
#include "core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace csn {
namespace fs = std::filesystem;

static RationalRect ParseRect(const nlohmann::json& j) {
    RationalRect r;
    r.left = j.value("roi_left", 0.0);
    r.top = j.value("roi_top", 0.0);
    r.right = j.value("roi_right", 1.0);
    r.bottom = j.value("roi_bottom", 1.0);
    return r;
}

static nlohmann::json RectToJson(const RationalRect& r) {
    return {
        {"roi_left", r.left},
        {"roi_top", r.top},
        {"roi_right", r.right},
        {"roi_bottom", r.bottom}
    };
}

bool LoadConfig(const fs::path& path, Config& out) {
    std::ifstream f(path.string());
    if (!f) {
        CSN_LOG_ERROR("Failed to open config file: " + path.string());
        return false;
    }
    try {
        nlohmann::json j;
        f >> j;

        const auto& win = j.value("window", nlohmann::json::object());
        out.window_title_substring = win.value("title_substring", std::string{"使命召唤手游"});
        out.capture_fps = win.value("capture_fps", 10);
        out.analysis_scale = win.value("analysis_scale", 0.5);

        const auto& hud = j.value("hud", nlohmann::json::object());
        out.hud_roi = ParseRect(hud);
        out.hud_template_paths = hud.value("template_paths", std::vector<std::string>{});
        out.hud_match_threshold = hud.value("match_threshold", 0.65);

        const auto& res = j.value("result", nlohmann::json::object());
        out.result_roi = ParseRect(res);
        out.result_keywords = res.value("keywords", std::vector<std::string>{"胜利", "战败", "失败", "VICTORY", "DEFEAT"});
        out.result_confidence_threshold = res.value("confidence_threshold", 0.6);
        out.result_upscale_min_height = res.value("upscale_min_height", 360);

        const auto& sm = j.value("state_machine", nlohmann::json::object());
        out.hud_missing_frames_to_die = sm.value("hud_missing_frames_to_die", 3);
        out.result_confirm_frames = sm.value("result_confirm_frames", 2);

        const auto& vid = j.value("video", nlohmann::json::object());
        out.video_target = vid.value("target", std::string{"chrome_douyin"});

        const auto& foc = j.value("focus", nlohmann::json::object());
        out.focus_switch_back_delay_ms = foc.value("switch_back_delay_ms", 100);

        out.diagnostic_mode = j.value("diagnostic_mode", false);
    } catch (const std::exception& e) {
        CSN_LOG_ERROR("Config parse error: " + std::string(e.what()));
        return false;
    }
    return true;
}

bool SaveConfig(const fs::path& path, const Config& cfg) {
    try {
        nlohmann::json j;
        j["window"] = {
            {"title_substring", cfg.window_title_substring},
            {"capture_fps", cfg.capture_fps},
            {"analysis_scale", cfg.analysis_scale}
        };
        j["hud"] = RectToJson(cfg.hud_roi);
        j["hud"]["template_paths"] = cfg.hud_template_paths;
        j["hud"]["match_threshold"] = cfg.hud_match_threshold;

        j["result"] = RectToJson(cfg.result_roi);
        j["result"]["keywords"] = cfg.result_keywords;
        j["result"]["confidence_threshold"] = cfg.result_confidence_threshold;
        j["result"]["upscale_min_height"] = cfg.result_upscale_min_height;

        j["state_machine"] = {
            {"hud_missing_frames_to_die", cfg.hud_missing_frames_to_die},
            {"result_confirm_frames", cfg.result_confirm_frames}
        };
        j["video"] = {{"target", cfg.video_target}};
        j["focus"] = {{"switch_back_delay_ms", cfg.focus_switch_back_delay_ms}};
        j["diagnostic_mode"] = cfg.diagnostic_mode;

        std::ofstream f(path.string());
        if (!f) {
            CSN_LOG_ERROR("Failed to write config file: " + path.string());
            return false;
        }
        f << j.dump(2);
    } catch (const std::exception& e) {
        CSN_LOG_ERROR("Config save error: " + std::string(e.what()));
        return false;
    }
    return true;
}

} // namespace csn
