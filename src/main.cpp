#include "core/config.h"
#include "core/logger.h"
#include "core/types.h"
#include "capture/screen_capture.h"
#include "detection/result_detector.h"
#include "state/state_machine.h"
#include "focus/focus_controller.h"
#include "video/ivideo_target.h"
#include "video/browser_video_target.h"

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
                      << "Detects respawn text (death) and result text (round/match win/lose).\n"
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

        ResultDetector respawn_detector;
        respawn_detector.SetRoi(cfg.respawn_roi);
        respawn_detector.SetKeywords(cfg.respawn_keywords);
        respawn_detector.SetConfidenceThreshold(cfg.respawn_confidence_threshold);
        respawn_detector.SetUpscaleMinHeight(cfg.respawn_upscale_min_height);

        ResultDetector result_detector;
        result_detector.SetRoi(cfg.result_roi);
        result_detector.SetKeywords(cfg.result_keywords);
        result_detector.SetConfidenceThreshold(cfg.result_confidence_threshold);
        result_detector.SetUpscaleMinHeight(cfg.result_upscale_min_height);

        ScreenCapture capture;

        if (diagnose) {
            std::filesystem::create_directories("diag_crops");
            std::ofstream diag("csn-diagnose.log", std::ios::trunc);
            diag << "t,respawn_keyword,respawn_raw,result_keyword,result_raw,sm_state\n" << std::flush;

            auto emit = [&](const std::string& line) {
                CSN_LOG_INFO(line);
                diag << line << "\n" << std::flush;
                std::cout << line << "\n";
            };

            struct DiagState {
                bool init = false;
                bool respawn_found = false;
                bool result_found = false;
                std::string respawn_kw;
                std::string result_kw;
                std::string respawn_raw;
                std::string result_raw;
                long long last_hb = 0;
                long long last_crop = 0;
                long long start = 0;
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
                emit(NowStamp() + " [SM] WOULD switch_to_video (respawn text confirmed)");
            };
            diag_deps.switch_back_to_game = [&]() {
                emit(NowStamp() + " [SM] WOULD switch_back_to_game (result confirmed)");
            };
            diag_deps.on_result_confirmed = [&]() {
                emit(NowStamp() + " [SM] result confirmed -> round reset");
            };
            StateMachine diag_sm(diag_deps);
            diag_sm.SetConfig(cfg.respawn_confirm_frames, cfg.result_confirm_frames,
                              cfg.respawn_absent_frames);

            capture.Start(game_hwnd, cfg.capture_fps, [&](const cv::Mat& frame, int, int, int) {
                cv::Mat scaled;
                if (cfg.analysis_scale > 0.0 && cfg.analysis_scale < 1.0) {
                    cv::resize(frame, scaled, cv::Size(), cfg.analysis_scale, cfg.analysis_scale, cv::INTER_LINEAR);
                } else {
                    scaled = frame;
                }

                RespawnText respawn = respawn_detector.Detect(scaled);
                // Same banner handling as the live path: "炸弹已被安装" etc. are
                // noise and must not change the state. Mark the frame ignored so
                // the diagnostic reflects the real decision (state preserved).
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

                // Drive the real StateMachine so the diagnostic log reflects the
                // exact same death/result decisions the live build makes.
                diag_sm.Update(respawn, result);
                st.respawn_raw = respawn.raw_text;
                st.result_raw = result.raw_text;

                if (!st.init) {
                    emit(NowStamp() + " Initial respawn=" + (respawn.found ? respawn.matched_keyword : "-")
                         + " result=" + (result.found ? result.matched_keyword : "-"));
                    st.respawn_found = respawn.found;
                    st.result_found = result.found;
                    st.respawn_kw = respawn.matched_keyword;
                    st.result_kw = result.matched_keyword;
                    st.init = true;
                } else {
                    if (respawn.found != st.respawn_found) {
                        emit(NowStamp() + " RESPAWN " + (st.respawn_found ? "FOUND" : "-")
                             + " -> " + (respawn.found ? respawn.matched_keyword : "-")
                             + " raw=[" + respawn.raw_text + "]");
                        st.respawn_found = respawn.found;
                        st.respawn_kw = respawn.matched_keyword;
                    }
                    if (result.found != st.result_found) {
                        emit(NowStamp() + " RESULT " + (st.result_found ? st.result_kw : "-")
                             + " -> " + (result.found ? result.matched_keyword : "-")
                             + " raw=[" + result.raw_text + "]");
                        st.result_found = result.found;
                        st.result_kw = result.matched_keyword;
                    }
                }

                long long now = NowMs();
                if (now - st.last_hb > 2000) {
                    st.last_hb = now;
                    std::string hb = NowStamp() + " [hb] respawn="
                                  + (respawn.found ? respawn.matched_keyword : "-")
                                  + " rraw=[" + respawn.raw_text + "]"
                                  + " result=" + (result.found ? result.matched_keyword : "-")
                                  + " sraw=[" + result.raw_text + "]"
                                  + " sm=" + SmStateName(diag_sm.GetState());
                    diag << hb << "\n" << std::flush;
                    std::cout << hb << "\n";
                }

                if (cfg.diagnostic_crop_interval_seconds > 0 &&
                    now - st.last_crop > cfg.diagnostic_crop_interval_seconds * 1000) {
                    st.last_crop = now;
                    std::string ts = std::to_string(now / 1000);
                    SaveCrop(frame, cfg.respawn_roi, "diag_crops/respawn_" + ts + ".png");
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

        // Companion page is enabled only when a url is configured.
        // Empty url -> no switching at all (user drives the page manually).
        std::unique_ptr<IVideoTarget> video_target;
        if (!cfg.companion_url.empty()) {
            video_target = std::make_unique<BrowserVideoTarget>(
                Utf8ToWide(cfg.companion_url),
                cfg.companion_app_mode,
                cfg.companion_fullscreen,
                Utf8ToWide(cfg.companion_browser_path));
        }

        StateMachine::Dependencies deps;
        deps.switch_to_video = [&]() {
            if (!video_target) return;
            HWND v = video_target->Show(game_hwnd);
            if (v) focus.SwitchToWindow(v);
        };
        deps.switch_back_to_game = [&]() {
            if (video_target) video_target->Hide(game_hwnd);
            focus.SwitchToWindow(game_hwnd);
        };
        deps.on_result_confirmed = [&]() {
            // Focus switch is already handled by the state machine callback.
        };

        StateMachine sm(deps);
        sm.SetConfig(cfg.respawn_confirm_frames, cfg.result_confirm_frames,
                     cfg.respawn_absent_frames);

        capture.Start(game_hwnd, cfg.capture_fps, [&](const cv::Mat& frame, int w, int h, int dpi) {
            cv::Mat scaled;
            if (cfg.analysis_scale > 0.0 && cfg.analysis_scale < 1.0) {
                cv::resize(frame, scaled, cv::Size(), cfg.analysis_scale, cfg.analysis_scale, cv::INTER_LINEAR);
            } else {
                scaled = frame;
            }

            RespawnText respawn = respawn_detector.Detect(scaled);
            // In-round banners ("炸弹已被安装" / "炸弹已被拆除" / ...) occupy the
            // same bottom-center area as the respawn hint and replace it for a
            // few seconds. They must NOT affect the state decision at all: if we
            // are in-game they must not trigger the video, and if we are on the
            // video they must not switch back (the respawn text returns later).
            // So we mark the frame as ignored and let the state machine keep the
            // current state unchanged.
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
