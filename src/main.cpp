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
#include <cstdlib>
#include <exception>
#include <conio.h>
#include <filesystem>
#include <algorithm>

#include <opencv2/opencv.hpp>

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

void LogFatal(const std::string& msg) {
    try {
        std::ofstream f("codmspawn_snack.log", std::ios::app);
        if (f) f << "[FATAL] " << msg << "\n";
    } catch (...) {
        // ignore secondary failures
    }
}

void SetupTerminationHandler() {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::set_terminate([] {
        LogFatal("std::terminate called (unhandled exception in some thread); aborting.");
        std::abort();
    });
}

bool HasCommandLineFlag(const std::wstring& flag) {
    std::wstring cmd = GetCommandLineW();
    return cmd.find(flag) != std::wstring::npos;
}

int ParseTimeoutSeconds(const std::wstring& prefix) {
    std::wstring cmd = GetCommandLineW();
    size_t pos = cmd.find(prefix);
    if (pos == std::wstring::npos) return 0;
    pos += prefix.size();
    // Skip optional '=' or space.
    while (pos < cmd.size() && (cmd[pos] == L' ' || cmd[pos] == L'=')) ++pos;
        int value = 0;
        while (pos < cmd.size() && cmd[pos] >= L'0' && cmd[pos] <= L'9') {
            value = value * 10 + (cmd[pos] - L'0');
            ++pos;
        }
        return value > 0 ? value : 0;
}

void AttachOrAllocDiagnosticConsole() {
    // Try to attach to the parent terminal so Ctrl+C works when launched from
    // cmd/PowerShell. If there is no parent console (double-click), allocate one.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    // stdin is also needed for _kbhit/_getch to work in the allocated console.
    freopen("CONIN$", "r", stdin);
}

struct PixelRect {
    int x = 0, y = 0, w = 0, h = 0;
};

PixelRect ToPixelRect(const csn::RationalRect& r, int fw, int fh) {
    PixelRect pr;
    pr.x = static_cast<int>(std::clamp(r.left, 0.0, 1.0) * fw);
    pr.y = static_cast<int>(std::clamp(r.top, 0.0, 1.0) * fh);
    int right = static_cast<int>(std::clamp(r.right, 0.0, 1.0) * fw);
    int bottom = static_cast<int>(std::clamp(r.bottom, 0.0, 1.0) * fh);
    pr.w = std::max(1, right - pr.x);
    pr.h = std::max(1, bottom - pr.y);
    return pr;
}

void SaveCrop(const cv::Mat& frame, const csn::RationalRect& roi,
              const std::filesystem::path& path) {
    try {
        PixelRect pr = ToPixelRect(roi, frame.cols, frame.rows);
        if (pr.x >= frame.cols || pr.y >= frame.rows) return;
        int x2 = std::min(pr.x + pr.w, frame.cols);
        int y2 = std::min(pr.y + pr.h, frame.rows);
        cv::Mat crop = frame(cv::Rect(pr.x, pr.y, x2 - pr.x, y2 - pr.y));
        cv::imwrite(path.string(), crop);
    } catch (const std::exception& e) {
        CSN_LOG_WARN("Failed to save diagnostic crop: " + std::string(e.what()));
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        using namespace csn;

        SetupTerminationHandler();
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);

        bool cli_diagnose = HasCommandLineFlag(L"--diagnose");
        bool cli_help = HasCommandLineFlag(L"--help") || HasCommandLineFlag(L"-h");
        int timeout_sec = ParseTimeoutSeconds(L"--timeout");

        Logger::Instance().SetFile("codmspawn_snack.log");
        CSN_LOG_INFO("CODMSpawnSnack starting.");

        if (cli_help) {
            AllocConsole();
            freopen("CONOUT$", "w", stdout);
            std::cout << "Usage:\n"
                      << "  CODMSpawnSnack.exe\n"
                      << "  CODMSpawnSnack.exe --diagnose [--timeout <sec>]\n"
                      << "\n"
                      << "--diagnose   Only detect and log; no focus/video switching.\n"
                      << "--timeout    Stop automatically after N seconds.\n"
                      << "\n"
                      << "In diagnostic mode press Ctrl+C or 'q' to stop.\n";
            return 0;
        }

        Config cfg;
        if (!LoadConfig(L"config.json", cfg)) {
            CSN_LOG_ERROR("Failed to load config.json; using defaults and writing example.");
            SaveConfig(L"config.json", cfg);
        }

        const bool diagnose = cfg.diagnostic_mode || cli_diagnose;

        if (diagnose) {
            AttachOrAllocDiagnosticConsole();
            std::cout << "=== CODMSpawnSnack DIAGNOSTIC MODE ===\n"
                      << "Detects HUD (alive/dead) and result text (round/match win/lose).\n"
                      << "No focus switching, no video launch.\n"
                      << "Log file: csn-diagnose.log\n"
                      << "Crops saved to: diag_crops/\n"
                      << "Stop with Ctrl+C or press 'q' (and Enter if using AllocConsole).\n";
            if (timeout_sec > 0) {
                std::cout << "Auto-stop after " << timeout_sec << " seconds.\n";
            }
            std::cout << std::endl;
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
        hud_detector.SetAbsentThreshold(cfg.hud_absent_threshold);
        hud_detector.SetTemplates(cfg.hud_template_paths);
        hud_detector.SetEquipmentTemplate(cfg.equipment_template_path);
        hud_detector.SetEquipmentRoi(cfg.equipment_roi);
        hud_detector.SetEquipmentThreshold(cfg.equipment_match_threshold);

        ResultDetector result_detector;
        result_detector.SetRoi(cfg.result_roi);
        result_detector.SetKeywords(cfg.result_keywords);
        result_detector.SetConfidenceThreshold(cfg.result_confidence_threshold);
        result_detector.SetUpscaleMinHeight(cfg.result_upscale_min_height);

        ScreenCapture capture;

        if (diagnose) {
            std::filesystem::create_directories("diag_crops");
            std::ofstream diag("csn-diagnose.log", std::ios::trunc);
            diag << "t,hud_state,hud_conf,result_keyword,result_conf,raw_text,absent_raw,sm_state\n" << std::flush;

            auto emit = [&](const std::string& line) {
                CSN_LOG_INFO(line);
                diag << line << "\n" << std::flush;
                std::cout << line << "\n";
            };

            struct DiagState {
                bool init = false;
                bool hud_present = false;
                bool have_result = false;
                std::string kw;
                std::string raw_text;
                long long last_hb = 0;
                long long last_crop = 0;
                long long start = 0;
                long long first_absent_ms = 0;
            } st;
            st.start = NowMs();

            auto SmStateName = [](StateMachine::State s) -> std::string {
                switch (s) {
                    case StateMachine::State::Idle:    return "Idle";
                    case StateMachine::State::InGame:  return "InGame";
                    case StateMachine::State::OnVideo: return "OnVideo";
                    default:                            return "?";
                }
            };

            // Diagnostic mode drives the REAL StateMachine with no-op switch
            // callbacks that only log. This makes the diagnostic log a faithful
            // "standard" mirror of live behavior: every death / result / respawn
            // decision the live build would make is recorded here, without
            // actually switching windows.
            StateMachine::Dependencies diag_deps;
            diag_deps.switch_to_video = [&]() {
                emit(NowStamp() + " [SM] WOULD switch_to_video (death delay elapsed, no result/respawn)");
            };
            diag_deps.switch_back_to_game = [&]() {
                emit(NowStamp() + " [SM] WOULD switch_back_to_game (respawn or result confirmed)");
            };
            diag_deps.on_result_confirmed = [&]() {
                emit(NowStamp() + " [SM] result confirmed -> round reset, switch CANCELLED");
            };
            StateMachine diag_sm(diag_deps);
            diag_sm.SetConfig(cfg.hud_missing_frames_to_die, cfg.result_confirm_frames,
                              cfg.death_switch_delay_ms, cfg.hud_respawn_frames);

            capture.Start(game_hwnd, cfg.capture_fps, [&](const cv::Mat& frame, int, int, int) {
                cv::Mat scaled;
                if (cfg.analysis_scale > 0.0 && cfg.analysis_scale < 1.0) {
                    cv::resize(frame, scaled, cv::Size(), cfg.analysis_scale, cfg.analysis_scale, cv::INTER_LINEAR);
                } else {
                    scaled = frame;
                }

                HudResult hud = hud_detector.Detect(scaled);
                ResultText result = result_detector.Detect(scaled);
                EquipmentResult equipment = hud_detector.DetectEquipment(scaled);
                bool present = (hud.presence == HudResult::Presence::Present);

                // Drive the real StateMachine so the diagnostic log reflects the
                // exact same death/result/respawn decisions the live build makes.
                diag_sm.Update(hud, result, equipment);
                st.raw_text = result.raw_text;
                {
                    long long now_d = NowMs();
                    if (present) st.first_absent_ms = 0;
                    else if (st.first_absent_ms == 0) st.first_absent_ms = now_d;
                }

                if (!st.init) {
                    emit(NowStamp() + " HUD initial state: " + (present ? "Present" : "Absent")
                         + " conf=" + std::to_string(hud.confidence));
                    st.hud_present = present;
                    st.init = true;
                } else if (present != st.hud_present) {
                    emit(NowStamp() + " HUD " + (st.hud_present ? "Present" : "Absent") + " -> "
                         + (present ? "Present" : "Absent") + " conf=" + std::to_string(hud.confidence));
                    st.hud_present = present;
                }

                if (result.found) {
                    if (!st.have_result || result.matched_keyword != st.kw) {
                        emit(NowStamp() + " RESULT keyword='" + result.matched_keyword
                             + "' conf=" + std::to_string(result.confidence)
                             + " raw_text=[" + result.raw_text + "]");
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
                    std::string absent_raw_str = "-";
                    if (!present && st.first_absent_ms > 0) {
                        double sec = (now - st.first_absent_ms) / 1000.0;
                        absent_raw_str = std::to_string(sec) + "s";
                    }
                    std::string hb = NowStamp() + " [hb] hud=" + (present ? "Present" : "Absent")
                                  + " conf=" + std::to_string(hud.confidence)
                                  + " absent_raw=" + absent_raw_str
                                  + " equip=" + (equipment.found ? "YES" : "no")
                                  + " equip_conf=" + std::to_string(equipment.confidence)
                                  + " result=" + (result.found ? result.matched_keyword : "-")
                                  + " raw_text=[" + result.raw_text + "]"
                                  + " sm=" + SmStateName(diag_sm.GetState());
                    diag << hb << "\n" << std::flush;
                    std::cout << hb << "\n";
                }

                if (cfg.diagnostic_crop_interval_seconds > 0 &&
                    now - st.last_crop > cfg.diagnostic_crop_interval_seconds * 1000) {
                    st.last_crop = now;
                    std::string ts = std::to_string(now / 1000);
                    SaveCrop(frame, cfg.hud_roi, "diag_crops/hud_" + ts + ".png");
                    SaveCrop(frame, cfg.result_roi, "diag_crops/result_" + ts + ".png");
                }
            });

            CSN_LOG_INFO("Diagnostic capture started. Press Ctrl+C or 'q' to stop.");
            std::cout << "Diagnostic capture started. Play a match, then press Ctrl+C or 'q'.\n";

            while (g_running) {
                if (timeout_sec > 0) {
                    long long elapsed = (NowMs() - st.start) / 1000;
                    if (elapsed >= timeout_sec) {
                        std::cout << "Auto-stopping after " << timeout_sec << " seconds.\n";
                        break;
                    }
                }
                if (_kbhit()) {
                    int ch = _getch();
                    if (ch == 'q' || ch == 'Q' || ch == 27) {  // 27 = ESC
                        g_running = false;
                        break;
                    }
                }
                Sleep(100);
            }

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
        sm.SetConfig(cfg.hud_missing_frames_to_die, cfg.result_confirm_frames,
                     cfg.death_switch_delay_ms, cfg.hud_respawn_frames);

        capture.Start(game_hwnd, cfg.capture_fps, [&](const cv::Mat& frame, int w, int h, int dpi) {
            cv::Mat scaled;
            if (cfg.analysis_scale > 0.0 && cfg.analysis_scale < 1.0) {
                cv::resize(frame, scaled, cv::Size(), cfg.analysis_scale, cfg.analysis_scale, cv::INTER_LINEAR);
            } else {
                scaled = frame;
            }

            HudResult hud = hud_detector.Detect(scaled);
            ResultText result = result_detector.Detect(scaled);
            EquipmentResult equipment = hud_detector.DetectEquipment(scaled);

            sm.Update(hud, result, equipment);
        });

        CSN_LOG_INFO("Capture started. Press Ctrl+C to stop.");
        if (timeout_sec > 0) {
            CSN_LOG_INFO("Auto-stop after " + std::to_string(timeout_sec) + " seconds.");
        }

        long long live_start = NowMs();
        while (g_running) {
            if (timeout_sec > 0) {
                long long elapsed = (NowMs() - live_start) / 1000;
                if (elapsed >= timeout_sec) {
                    CSN_LOG_INFO("Auto-stopping after " + std::to_string(timeout_sec) + " seconds.");
                    break;
                }
            }
            Sleep(100);
        }

        capture.Stop();
        CSN_LOG_INFO("Exiting.");
        return 0;
    } catch (const std::exception& e) {
        LogFatal(std::string("Unhandled exception in main: ") + e.what());
        return 1;
    } catch (...) {
        LogFatal("Unhandled unknown exception in main.");
        return 1;
    }
}
