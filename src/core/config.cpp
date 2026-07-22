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

        const auto& resp = j.value("respawn", nlohmann::json::object());
        out.respawn_roi = ParseRect(resp);
        out.respawn_keywords = resp.value("keywords", std::vector<std::string>{"你将在下一回合重生", "下一回合重生", "重生"});
        out.respawn_confidence_threshold = resp.value("confidence_threshold", 0.6);
        out.respawn_upscale_min_height = resp.value("upscale_min_height", 160);

        const auto& res = j.value("result", nlohmann::json::object());
        out.result_roi = ParseRect(res);
        out.result_keywords = res.value("keywords", std::vector<std::string>{"胜利", "战败", "失败", "VICTORY", "DEFEAT"});
        out.result_confidence_threshold = res.value("confidence_threshold", 0.6);
        out.result_upscale_min_height = res.value("upscale_min_height", 360);

        const auto& sm = j.value("state_machine", nlohmann::json::object());
        out.respawn_confirm_frames = sm.value("respawn_confirm_frames", 5);
        out.result_confirm_frames = sm.value("result_confirm_frames", 2);
        out.respawn_absent_frames = sm.value("respawn_absent_frames", 20);

        // Read the companion section; fall back to the old "video" key for
        // backward compatibility with earlier configs.
        const auto& comp = j.value("companion", j.value("video", nlohmann::json::object()));
        out.companion_url = comp.value("url", std::string{"https://www.bilibili.com"});
        if (out.companion_url.empty()) {
            // Backwards-compatible mapping of the old "target" preset.
            std::string t = comp.value("target", std::string{});
            if (t == "chrome_kuaishou") out.companion_url = "https://www.kuaishou.com";
            else out.companion_url = "https://www.bilibili.com";
        }
        out.companion_app_mode = comp.value("app_mode", true);
        out.companion_fullscreen = comp.value("fullscreen", true);
        out.companion_browser_path = comp.value("browser_path", std::string{"msedge.exe"});

        const auto& foc = j.value("focus", nlohmann::json::object());
        out.focus_switch_back_delay_ms = foc.value("switch_back_delay_ms", 100);

        out.diagnostic_mode = j.value("diagnostic_mode", false);
        out.diagnostic_crop_interval_seconds = j.value("diagnostic_crop_interval_seconds", 10);
    } catch (const std::exception& e) {
        CSN_LOG_ERROR("Config parse error: " + std::string(e.what()));
        return false;
    }
    return true;
}

bool SaveConfig(const fs::path& path, const Config& cfg) {
    try {
        nlohmann::json j = ConfigToJson(cfg);
        j["window"] = {
            {"title_substring", cfg.window_title_substring},
            {"capture_fps", cfg.capture_fps},
            {"analysis_scale", cfg.analysis_scale}
        };

        j["respawn"] = RectToJson(cfg.respawn_roi);
        j["respawn"]["keywords"] = cfg.respawn_keywords;
        j["respawn"]["confidence_threshold"] = cfg.respawn_confidence_threshold;
        j["respawn"]["upscale_min_height"] = cfg.respawn_upscale_min_height;

        j["result"] = RectToJson(cfg.result_roi);
        j["result"]["keywords"] = cfg.result_keywords;
        j["result"]["confidence_threshold"] = cfg.result_confidence_threshold;
        j["result"]["upscale_min_height"] = cfg.result_upscale_min_height;

        j["state_machine"] = {
            {"respawn_confirm_frames", cfg.respawn_confirm_frames},
            {"result_confirm_frames", cfg.result_confirm_frames},
            {"respawn_absent_frames", cfg.respawn_absent_frames}
        };
        j["companion"] = {
            {"url", cfg.companion_url},
            {"app_mode", cfg.companion_app_mode},
            {"fullscreen", cfg.companion_fullscreen},
            {"browser_path", cfg.companion_browser_path}
        };
        j["focus"] = {{"switch_back_delay_ms", cfg.focus_switch_back_delay_ms}};
        j["diagnostic_mode"] = cfg.diagnostic_mode;
        j["diagnostic_crop_interval_seconds"] = cfg.diagnostic_crop_interval_seconds;

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

nlohmann::json ConfigToJson(const Config& cfg) {
    nlohmann::json j;
    j["window"] = {
        {"title_substring", cfg.window_title_substring},
        {"capture_fps", cfg.capture_fps},
        {"analysis_scale", cfg.analysis_scale}
    };

    j["respawn"] = RectToJson(cfg.respawn_roi);
    j["respawn"]["keywords"] = cfg.respawn_keywords;
    j["respawn"]["confidence_threshold"] = cfg.respawn_confidence_threshold;
    j["respawn"]["upscale_min_height"] = cfg.respawn_upscale_min_height;

    j["result"] = RectToJson(cfg.result_roi);
    j["result"]["keywords"] = cfg.result_keywords;
    j["result"]["confidence_threshold"] = cfg.result_confidence_threshold;
    j["result"]["upscale_min_height"] = cfg.result_upscale_min_height;

    j["state_machine"] = {
        {"respawn_confirm_frames", cfg.respawn_confirm_frames},
        {"result_confirm_frames", cfg.result_confirm_frames},
        {"respawn_absent_frames", cfg.respawn_absent_frames}
    };
    j["companion"] = {
        {"url", cfg.companion_url},
        {"app_mode", cfg.companion_app_mode},
        {"fullscreen", cfg.companion_fullscreen},
        {"browser_path", cfg.companion_browser_path}
    };
    j["focus"] = {{"switch_back_delay_ms", cfg.focus_switch_back_delay_ms}};
    j["diagnostic_mode"] = cfg.diagnostic_mode;
    j["diagnostic_crop_interval_seconds"] = cfg.diagnostic_crop_interval_seconds;
    return j;
}

} // namespace csn
