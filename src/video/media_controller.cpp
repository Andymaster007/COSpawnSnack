#include "video/media_controller.h"
#include "core/logger.h"

#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <string>

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

// Find the media session belonging to our browser. Prefer a session whose
// SourceAppUserModelId contains the browser app-id; if none matches but exactly
// one session exists system-wide, use it as a fallback.
GlobalSystemMediaTransportControlsSession FindSession(const std::wstring& browser_exe) {
    std::wstring want = AppIdSubstring(browser_exe);
    try {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (!manager) return nullptr;
        auto sessions = manager.GetSessions();
        GlobalSystemMediaTransportControlsSession fallback{ nullptr };
        for (const auto& s : sessions) {
            std::wstring app = ToLower(std::wstring(s.SourceAppUserModelId().c_str()));
            if (!app.empty() && app.find(want) != std::wstring::npos) {
                return s;
            }
            if (!fallback) fallback = s;
        }
        if (sessions.Size() == 1) return fallback;
    } catch (const winrt::hresult_error& e) {
        CSN_LOG_WARN("MediaController: GSMTCS query failed: code " +
                     std::to_string(static_cast<int>(e.code())));
    } catch (...) {
        CSN_LOG_WARN("MediaController: GSMTCS query threw an exception.");
    }
    return nullptr;
}

} // namespace

MediaController::MediaController(std::wstring browser_exe)
    : browser_exe_(ToLower(std::move(browser_exe))) {}

MediaController::~MediaController() = default;

bool MediaController::IsPlaying() {
    auto s = FindSession(browser_exe_);
    if (!s) return false;
    try {
        return MapStatus(s.GetPlaybackInfo().PlaybackStatus()) == PlaybackStatus::Playing;
    } catch (...) {
        return false;
    }
}

void MediaController::Pause() {
    auto s = FindSession(browser_exe_);
    if (!s) {
        // Expected for non-video companion pages (blogs, academic sites, ...):
        // there is simply nothing to pause. Not an error.
        CSN_LOG_INFO("MediaController::Pause: no media session found (nothing playing).");
        return;
    }
    try {
        PlaybackStatus st = MapStatus(s.GetPlaybackInfo().PlaybackStatus());
        if (st == PlaybackStatus::Playing) {
            s.TryPauseAsync().get();
            CSN_LOG_INFO("MediaController: sent Pause (was Playing).");
        } else {
            CSN_LOG_INFO(std::string("MediaController: Pause skipped (status=") +
                         StatusName(st) + ", already not playing).");
        }
    } catch (const winrt::hresult_error& e) {
        CSN_LOG_WARN("MediaController::Pause failed: code " +
                     std::to_string(static_cast<int>(e.code())));
    } catch (...) {
        CSN_LOG_WARN("MediaController::Pause threw an exception.");
    }
}

void MediaController::Play() {
    auto s = FindSession(browser_exe_);
    if (!s) {
        // Expected for non-video companion pages: nothing to play.
        CSN_LOG_INFO("MediaController::Play: no media session found (nothing to play).");
        return;
    }
    try {
        PlaybackStatus st = MapStatus(s.GetPlaybackInfo().PlaybackStatus());
        if (st != PlaybackStatus::Playing) {
            s.TryPlayAsync().get();
            CSN_LOG_INFO("MediaController: sent Play (was " + std::string(StatusName(st)) + ").");
        } else {
            CSN_LOG_INFO("MediaController: Play skipped (already Playing).");
        }
    } catch (const winrt::hresult_error& e) {
        CSN_LOG_WARN("MediaController::Play failed: code " +
                     std::to_string(static_cast<int>(e.code())));
    } catch (...) {
        CSN_LOG_WARN("MediaController::Play threw an exception.");
    }
}

void MediaController::LogStatus(const char* context) {
    auto s = FindSession(browser_exe_);
    if (!s) {
        CSN_LOG_INFO(std::string(context) + ": no media session found.");
        return;
    }
    try {
        PlaybackStatus st = MapStatus(s.GetPlaybackInfo().PlaybackStatus());
        CSN_LOG_INFO(std::string(context) + ": media status = " + StatusName(st));
    } catch (...) {
        CSN_LOG_INFO(std::string(context) + ": media status query failed.");
    }
}

} // namespace csn
