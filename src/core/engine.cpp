#include "core/engine.h"
#include "core/logger.h"
#include "detection/result_detector.h"
#include "capture/screen_capture.h"
#include "state/state_machine.h"

#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace csn {

Engine::Engine(std::shared_ptr<Config> config)
    : config_(std::move(config)) {}

Engine::~Engine() {
    Stop();
}

bool Engine::Start() {
    if (running_.load()) return false;
    running_ = true;
    thread_ = std::thread([this] { Worker(); });
    return true;
}

void Engine::Stop() {
    if (!running_.load()) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (status_cb_) status_cb_(false);
}

void Engine::Worker() {
    if (status_cb_) status_cb_(true);
    CSN_LOG_INFO("Engine started.");

    // Wait for the game window. Exact-title match is used so a wrapper window
    // ("...模拟器高清版") is never picked. Poll once per second so we don't
    // burn CPU, and bail out if Stop() is requested.
    HWND hwnd = nullptr;
    while (running_) {
        hwnd = focus_.FindWindowByTitle(Utf8ToWide(config_->window_title_substring));
        if (hwnd) break;
        CSN_LOG_INFO("Game window not found; waiting 1s...");
        for (int i = 0; i < 10 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!running_) {
        CSN_LOG_INFO("Engine stopped before game window appeared.");
        return;
    }
    game_hwnd_ = hwnd;
    CSN_LOG_INFO("Game window found; starting capture.");

    ResultDetector respawn_detector;
    respawn_detector.SetRoi(config_->respawn_roi);
    respawn_detector.SetKeywords(config_->respawn_keywords);
    respawn_detector.SetConfidenceThreshold(config_->respawn_confidence_threshold);
    respawn_detector.SetUpscaleMinHeight(config_->respawn_upscale_min_height);

    ResultDetector result_detector;
    result_detector.SetRoi(config_->result_roi);
    result_detector.SetKeywords(config_->result_keywords);
    result_detector.SetConfidenceThreshold(config_->result_confidence_threshold);
    result_detector.SetUpscaleMinHeight(config_->result_upscale_min_height);

    StateMachine::Dependencies deps;
    deps.switch_to_video = [this]() { SwitchToVideo(); };
    deps.switch_back_to_game = [this]() { SwitchBackToGame(); };
    deps.on_result_confirmed = []() {};
    StateMachine sm(deps);
    sm.SetConfig(config_->respawn_confirm_frames, config_->result_confirm_frames,
                 config_->respawn_absent_frames);

    ScreenCapture capture;
    capture.Start(game_hwnd_, config_->capture_fps,
                  [&](const cv::Mat& frame, int, int, int) {
                      cv::Mat scaled;
                      if (config_->analysis_scale > 0.0 && config_->analysis_scale < 1.0) {
                          cv::resize(frame, scaled, cv::Size(), config_->analysis_scale,
                                     config_->analysis_scale, cv::INTER_LINEAR);
                      } else {
                          scaled = frame;
                      }

                      RespawnText respawn = respawn_detector.Detect(scaled);
                      // In-round banners ("炸弹已被安装" etc.) occupy the same
                      // area as the respawn hint and must not change state.
                      {
                          const std::string& t = respawn.raw_text;
                          bool has_banner = t.find("炸弹") != std::string::npos ||
                                            t.find("安装") != std::string::npos ||
                                            t.find("拆除") != std::string::npos ||
                                            t.find("排除") != std::string::npos;
                          if (has_banner && !respawn.found) {
                              respawn.ignored = true;
                          }
                      }
                      ResultText result = result_detector.Detect(scaled);
                      sm.Update(respawn, result);
                  });

    CSN_LOG_INFO("Capture started. Monitoring...");
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    capture.Stop();
    CSN_LOG_INFO("Engine stopped.");
    if (status_cb_) status_cb_(false);
}

void Engine::EnsureVideoTarget() {
    std::lock_guard<std::mutex> lock(video_config_mutex_);
    if (config_->companion_url.empty()) {
        video_target_.reset();
        video_url_.clear();
        video_browser_.clear();
        return;
    }
    if (!video_target_ || video_url_ != config_->companion_url ||
        video_browser_ != config_->companion_browser_path) {
        video_target_ = std::make_unique<BrowserVideoTarget>(
            Utf8ToWide(config_->companion_url),
            config_->companion_app_mode,
            config_->companion_fullscreen,
            Utf8ToWide(config_->companion_browser_path));
        video_url_ = config_->companion_url;
        video_browser_ = config_->companion_browser_path;
    }
}

void Engine::SwitchToVideo() {
    EnsureVideoTarget();
    if (!video_target_) return;
    HWND v = video_target_->Show(game_hwnd_);
    if (v) focus_.SwitchToWindow(v);
}

void Engine::SwitchBackToGame() {
    if (video_target_) video_target_->Hide(game_hwnd_);
    focus_.SwitchToWindow(game_hwnd_);
}

void Engine::SetCompanion(const std::string& url, const std::string& browser_path) {
    std::lock_guard<std::mutex> lock(video_config_mutex_);
    config_->companion_url = url;
    config_->companion_browser_path = browser_path;
}

std::string Engine::GetCompanionUrl() const {
    std::lock_guard<std::mutex> lock(video_config_mutex_);
    return config_->companion_url;
}

std::string Engine::GetBrowserChoice() const {
    std::lock_guard<std::mutex> lock(video_config_mutex_);
    const std::string& p = config_->companion_browser_path;
    bool edge = p.find("msedge") != std::string::npos || p.find("edge") != std::string::npos;
    return edge ? "edge" : "chrome";
}

void Engine::TestSwitch() {
    HWND hwnd = focus_.FindWindowByTitle(Utf8ToWide(config_->window_title_substring));
    if (!hwnd) {
        CSN_LOG_WARN("TestSwitch: game window not found; nothing to switch against.");
        return;
    }
    EnsureVideoTarget();
    if (!video_target_) {
        CSN_LOG_WARN("TestSwitch: no companion URL configured; nothing to show.");
        return;
    }
    HWND v = video_target_->Show(hwnd);
    if (v) focus_.SwitchToWindow(v);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    video_target_->Hide(hwnd);
    focus_.SwitchToWindow(hwnd);
    CSN_LOG_INFO("TestSwitch: one show/hide cycle completed.");
}

} // namespace csn
