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
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <cstdio>

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

std::string NowStamp() {
    auto t = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[16]{};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return std::string(buf);
}

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    using namespace csn;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    bool cli_diagnose = std::wstring(GetCommandLineW()).find(L"--diagnose") != std::wstring::npos;

    Logger::Instance().SetFile("codmspawn_snack.log");
    CSN_LOG_INFO("CODMSpawnSnack starting.");

    Config cfg;
    if (!LoadConfig(L"config.json", cfg)) {
        CSN_LOG_ERROR("Failed to load config.json; using defaults and writing example.");
        SaveConfig(L"config.json", cfg);
    }

    const bool diagnose = cfg.diagnostic_mode || cli_diagnose;

    if (diagnose) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        std::cout << "=== CODMSpawnSnack DIAGNOSTIC MODE ===\n"
                  << "Detects HUD (alive/dead) and result text (round/match win/lose).\n"
                  << "No focus switching, no video launch.\n"
                  << "Log file: csn-diagnose.log\n"
                  << "Play a match, then stop with Ctrl+C.\n" << std::endl;
    }

    FocusController focus;
    std::wstring game_title = Utf8ToWide(cfg.window_title_substring);
    HWND game_hwnd = focus.FindWindowByTitle(game_title);
    if (!game_hwnd) {
        CSN_LOG_ERROR("Game window not found. Exiting.");
        std::cerr << "Game window not found. Exiting.\n";
        return 1;
    }

    HudDetector hud_detector;
    hud_detector.SetRoi(cfg.hud_roi);
    hud_detector.SetThreshold(cfg.hud_match_threshold);
    hud_detector.SetTemplates(cfg.hud_template_paths);

    ResultDetector result_detector;
    result_detector.SetRoi(cfg.result_roi);
    result_detector.SetKeywords(cfg.result_keywords);
    result_detector.SetConfidenceThreshold(cfg.result_confidence_threshold);
    result_detector.SetUpscaleMinHeight(cfg.result_upscale_min_height);

    ScreenCapture capture;

    if (diagnose) {
        std::ofstream diag("csn-diagnose.log", std::ios::trunc);
        diag << "t,hud_state,hud_conf,result_keyword,result_conf\n";

        struct DiagState {
            bool init = false;
            bool hud_present = true;
            bool have_result = false;
            std::string kw;
            long long last_hb = 0;
        } st;

        auto emit = [&](const std::string& line) {
            CSN_LOG_INFO(line);
            diag << line << "\n";
            std::cout << line << "\n";
        };

        capture.Start(game_hwnd, cfg.capture_fps, [&](const cv::Mat& frame, int, int, int) {
            cv::Mat scaled;
            if (cfg.analysis_scale > 0.0 && cfg.analysis_scale < 1.0) {
                cv::resize(frame, scaled, cv::Size(), cfg.analysis_scale, cfg.analysis_scale, cv::INTER_LINEAR);
            } else {
                scaled = frame;
            }

            HudResult hud = hud_detector.Detect(scaled);
            ResultText result = result_detector.Detect(scaled);
            bool present = (hud.presence == HudResult::Presence::Present);

            if (!st.init || present != st.hud_present) {
                emit(NowStamp() + " HUD " + (st.hud_present ? "Present" : "Absent") + " -> "
                     + (present ? "Present" : "Absent") + " conf=" + std::to_string(hud.confidence));
                st.hud_present = present;
                st.init = true;
            }

            if (result.found) {
                if (!st.have_result || result.matched_keyword != st.kw) {
                    emit(NowStamp() + " RESULT keyword='" + result.matched_keyword
                         + "' conf=" + std::to_string(result.confidence));
                    st.have_result = true;
                    st.kw = result.matched_keyword;
                }
            } else if (st.have_result) {
                st.have_result = false;
                st.kw.clear();
            }

            long long now = NowMs();
            if (now - st.last_hb > 2000) {
                st.last_hb = now;
                std::string hb = NowStamp() + " [hb] hud=" + (present ? "Present" : "Absent")
                              + " conf=" + std::to_string(hud.confidence)
                              + " result=" + (result.found ? result.matched_keyword : "-");
                diag << hb << "\n";
                std::cout << hb << "\n";
            }
        });

        CSN_LOG_INFO("Diagnostic capture started. Press Ctrl+C to stop.");
        std::cout << "Diagnostic capture started. Play a match, then press Ctrl+C to stop.\n";
        while (g_running) Sleep(100);
        capture.Stop();
        CSN_LOG_INFO("Exiting diagnostic mode.");
        return 0;
    }

    auto video_target = std::make_unique<ChromeDouyinTarget>();
    bool video_was_paused = false;

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
