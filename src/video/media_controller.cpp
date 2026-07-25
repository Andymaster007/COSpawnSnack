#include "video/media_controller.h"
#include "core/logger.h"

#include <windows.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace csn {

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Foundation::Collections;

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// Derive an app-id substring from the browser exe name, e.g.
// "chrome.exe" -> "chrome", "msedge.exe" -> "edge". Used to match the
// session's SourceAppUserModelId (e.g. "Chrome" or the Edge AUMID).
std::wstring AppIdSubstring(const std::wstring& browser_exe) {
    std::wstring s = ToLower(browser_exe);
    auto dot = s.rfind(L".exe");
    if (dot != std::wstring::npos) s = s.substr(0, dot);
    return s;
}

PlaybackStatus MapStatus(GlobalSystemMediaTransportControlsSessionPlaybackStatus s) {
    switch (s) {
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
            return PlaybackStatus::Playing;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            return PlaybackStatus::Paused;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
            return PlaybackStatus::Stopped;
        default:
            return PlaybackStatus::Other;
    }
}

const char* StatusName(PlaybackStatus s) {
    switch (s) {
        case PlaybackStatus::Playing: return "Playing";
        case PlaybackStatus::Paused:  return "Paused";
        case PlaybackStatus::Stopped: return "Stopped";
        case PlaybackStatus::Other:   return "Other";
        default:                       return "Unknown";
    }
}

// Enumerate ALL media sessions belonging to our browser. Matches every session
// whose SourceAppUserModelId contains the browser app-id (e.g. "Chrome" / the
// Edge AUMID).
std::vector<GlobalSystemMediaTransportControlsSession> FindSessions(const std::wstring& browser_exe) {
    std::wstring want = AppIdSubstring(browser_exe);
    std::vector<GlobalSystemMediaTransportControlsSession> out;
    try {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (!manager) return out;
        auto sessions = manager.GetSessions();
        for (const auto& s : sessions) {
            std::wstring app = ToLower(std::wstring(s.SourceAppUserModelId().c_str()));
            if (!app.empty() && app.find(want) != std::wstring::npos) {
                out.push_back(s);
            }
        }
    } catch (const winrt::hresult_error& e) {
        CSN_LOG_WARN("MediaController: GSMTCS query failed: code " +
                     std::to_string(static_cast<int>(e.code())));
    } catch (...) {
        CSN_LOG_WARN("MediaController: GSMTCS query threw an exception.");
    }
    return out;
}

} // namespace

MediaController::MediaController(std::wstring browser_exe)
    : browser_exe_(ToLower(std::move(browser_exe))) {}

MediaController::~MediaController() = default;

void MediaController::Pause() {
    GsmtcPause();
}

void MediaController::Play() {
    GsmtcPlay();
}

void MediaController::LogStatus(const char* context) {
    CSN_LOG_INFO(std::string(context) + ": GSMTC control channel active.");
    GsmtcLogStatus();
}

void MediaController::GsmtcPlay() {
    auto sessions = FindSessions(browser_exe_);
    if (sessions.empty()) {
        CSN_LOG_INFO("MediaController::GsmtcPlay: no media session found (nothing to play).");
        return;
    }
    int played = 0;
    for (const auto& s : sessions) {
        try {
            s.TryPlayAsync().get();
            ++played;
        } catch (const winrt::hresult_error& e) {
            CSN_LOG_WARN("MediaController::GsmtcPlay failed: code " +
                         std::to_string(static_cast<int>(e.code())));
        } catch (...) {
            CSN_LOG_WARN("MediaController::GsmtcPlay threw an exception.");
        }
    }
    CSN_LOG_INFO("MediaController: GsmtcPlay sent to " + std::to_string(played) +
                 " session(s) of " + std::to_string(sessions.size()) + " total.");
}

void MediaController::GsmtcPause() {
    auto sessions = FindSessions(browser_exe_);
    if (sessions.empty()) {
        CSN_LOG_INFO("MediaController::GsmtcPause: no media session found (nothing to pause).");
        return;
    }
    int paused = 0;
    for (const auto& s : sessions) {
        try {
            s.TryPauseAsync().get();
            ++paused;
        } catch (const winrt::hresult_error& e) {
            CSN_LOG_WARN("MediaController::GsmtcPause failed: code " +
                         std::to_string(static_cast<int>(e.code())));
        } catch (...) {
            CSN_LOG_WARN("MediaController::GsmtcPause threw an exception.");
        }
    }
    CSN_LOG_INFO("MediaController: GsmtcPause sent to " + std::to_string(paused) +
                 " session(s) of " + std::to_string(sessions.size()) + " total.");
}

void MediaController::GsmtcLogStatus() {
    auto sessions = FindSessions(browser_exe_);
    if (sessions.empty()) {
        CSN_LOG_INFO("GsmtcLogStatus: no media session found.");
        return;
    }
    try {
        PlaybackStatus st = MapStatus(sessions[0].GetPlaybackInfo().PlaybackStatus());
        CSN_LOG_INFO("GsmtcLogStatus: " + std::to_string(sessions.size()) +
                     " session(s), first = " + StatusName(st));
    } catch (...) {
        CSN_LOG_INFO("GsmtcLogStatus: media status query failed.");
    }
}

} // namespace csn
