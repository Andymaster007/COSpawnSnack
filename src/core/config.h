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

    // Respawn hint shown while spectating teammates after death in round-based
    // modes (e.g. "你将在下一回合重生"). Detected via OCR.
    RationalRect respawn_roi{0.30, 0.79, 0.61, 0.89};
    std::vector<std::string> respawn_keywords = {"你将在下一回合重生", "下一回合重生", "重生"};
    double respawn_confidence_threshold = 0.6;
    int respawn_upscale_min_height = 160;

    // Result text shown at round / match end (胜利/战败 etc.). Detected via OCR.
    RationalRect result_roi{0.30, 0.22, 0.70, 0.52};
    std::vector<std::string> result_keywords = {"胜利", "战败", "失败", "VICTORY", "DEFEAT"};
    double result_confidence_threshold = 0.6;
    // Before OCR, the result-text ROI is upscaled so its height reaches at
    // least this many pixels. Keeps recognition accuracy consistent across
    // device resolutions (text is tiny on low-res windows otherwise).
    int result_upscale_min_height = 360;

    int respawn_confirm_frames = 5;
    int result_confirm_frames = 2;
    // Consecutive respawn-ABSENT frames required before switching back to the
    // game. This fires when the player is alive again (respawned next round, or
    // the round/match ended without a readable 胜利/战败 banner). Set it high
    // enough that a single-frame OCR flicker -- or a transient in-round banner
    // such as "炸弹已被安装" (the respawn detector treats that banner as "still
    // present", so it does not even accumulate absence frames) -- cannot trigger
    // a premature switch-back. 20 frames @ 10fps ~= 2 seconds of sustained
    // absence is a safe default.
    int respawn_absent_frames = 20;

    // Any web page opened while the player is dead (a "companion" page shown
    // during downtime). It can be a video site (Douyin / Bilibili / Kuaishou)
    // OR any other site such as Xiaohongshu, a blog, or an academic page.
    // Video play/pause controls only take effect when the page actually has
    // media playing; on non-video sites the window is simply shown / hidden
    // with no side effects. Empty string disables companion switching entirely
    // (you drive the page yourself).
    std::string companion_url = "https://www.douyin.com";
    // true  -> open the URL in Chrome app mode (borderless dedicated window).
    // false -> open a normal browser window (--new-window).
    bool companion_app_mode = true;
    // Maximize the companion window on show (fullscreen-window mode).
    bool companion_fullscreen = true;
    // Explicit Chrome/Edge executable path. Empty -> "chrome.exe" on PATH with
    // common install-location fallbacks.
    std::string companion_browser_path;

    int focus_switch_back_delay_ms = 100;

    // When true, the program only detects (respawn + result text) and logs every
    // state change to csn-diagnose.log without switching focus or launching
    // video. Useful for verifying detection before trusting the auto-switch.
    bool diagnostic_mode = false;

    // How often to save debug ROI crops in diagnostic mode (0 = disabled).
    int diagnostic_crop_interval_seconds = 10;
};

bool LoadConfig(const std::filesystem::path& path, Config& out);
bool SaveConfig(const std::filesystem::path& path, const Config& cfg);

} // namespace csn
