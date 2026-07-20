#include "core/config.h"
#include "core/logger.h"
#include "core/types.h"
#include "capture/screen_capture.h"
#include "detection/hud_detector.h"
#include "detection/result_detector.h"
#include "state/state_machine.h"
#include "focus/focus_controller.h"
#include "video/ivideo_target.h"
#include "video/chrome_douyin_target.h"

#include <Windows.h>
#include <memory>
#include <string>
#include <atomic>

namespace {

std::atomic<bool> g_running{true};

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), size);
    return result;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    using namespace csn;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    Logger::Instance().SetFile("codmspawn_snack.log");
    CSN_LOG_INFO("CODMSpawnSnack starting.");

    Config cfg;
    if (!LoadConfig(L"config.json", cfg)) {
        CSN_LOG_ERROR("Failed to load config.json; using defaults and writing example.");
        SaveConfig(L"config.json", cfg);
    }

    FocusController focus;
    std::wstring game_title = Utf8ToWide(cfg.window_title_substring);
    HWND game_hwnd = focus.FindWindowByTitle(game_title);
    if (!game_hwnd) {
        CSN_LOG_ERROR("Game window not found. Exiting.");
        return 1;
    }

    auto video_target = std::make_unique<ChromeDouyinTarget>();
    bool video_was_paused = false;

    HudDetector hud_detector;
    hud_detector.SetRoi(cfg.hud_roi);
    hud_detector.SetThreshold(cfg.hud_match_threshold);
    hud_detector.SetTemplates(cfg.hud_template_paths);

    ResultDetector result_detector;
    result_detector.SetRoi(cfg.result_roi);
    result_detector.SetKeywords(cfg.result_keywords);
    result_detector.SetConfidenceThreshold(cfg.result_confidence_threshold);

    StateMachine::Dependencies deps;
    deps.switch_to_video = [&]() {
        video_target->Launch();
        if (video_was_paused) {
            video_target->Resume();
            video_was_paused = false;
        }
    };
    deps.switch_back_to_game = [&]() {
        video_target->Pause();
        video_was_paused = true;
        focus.SwitchToWindow(game_hwnd);
    };
    deps.on_result_confirmed = [&]() {
        // Focus switch is already handled by the state machine callback.
    };

    StateMachine sm(deps);
    sm.SetConfig(cfg.hud_missing_frames_to_die, cfg.result_confirm_frames);

    ScreenCapture capture;
    capture.Start(game_hwnd, cfg.capture_fps, [&](const cv::Mat& frame, int w, int h, int dpi) {
        cv::Mat scaled;
        if (cfg.analysis_scale > 0.0 && cfg.analysis_scale < 1.0) {
            cv::resize(frame, scaled, cv::Size(), cfg.analysis_scale, cfg.analysis_scale, cv::INTER_LINEAR);
        } else {
            scaled = frame;
        }

        HudResult hud = hud_detector.Detect(scaled);
        ResultText result = result_detector.Detect(scaled);

        sm.Update(hud, result);
    });

    CSN_LOG_INFO("Capture started. Press Ctrl+C to stop.");

    while (g_running) {
        Sleep(100);
    }

    capture.Stop();
    CSN_LOG_INFO("Exiting.");
    return 0;
}
