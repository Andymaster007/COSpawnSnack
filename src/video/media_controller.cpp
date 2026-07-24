#include "video/media_controller.h"
#include "core/logger.h"

#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
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
// Edge AUMID). Returns all matches so callers can pause/play precisely across
// the multiple tabs/pages that may be playing in the user's everyday browser.
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
    auto sessions = FindSessions(browser_exe_);
    if (sessions.empty()) {
        // Expected for non-video companion pages (blogs, academic sites, ...):
        // there is simply nothing to pause. Not an error.
        CSN_LOG_INFO("MediaController::Pause: no media session found (nothing playing).");
        return;
    }
    // Pause every session that is currently Playing. Already-paused sessions
    // are never touched, so we never accidentally resume something the user
    // paused manually. This keeps "pause" accurate even when several tabs in
    // the browser are playing media.
    int paused = 0;
    for (const auto& s : sessions) {
        try {
            PlaybackStatus st = MapStatus(s.GetPlaybackInfo().PlaybackStatus());
            if (st == PlaybackStatus::Playing) {
                s.TryPauseAsync().get();
                ++paused;
            }
        } catch (const winrt::hresult_error& e) {
            CSN_LOG_WARN("MediaController::Pause failed: code " +
                         std::to_string(static_cast<int>(e.code())));
        } catch (...) {
            CSN_LOG_WARN("MediaController::Pause threw an exception.");
        }
    }
    CSN_LOG_INFO("MediaController: Pause sent to " + std::to_string(paused) +
                 " playing session(s) of " + std::to_string(sessions.size()) + " total.");
}

void MediaController::Play() {
    auto sessions = FindSessions(browser_exe_);
    if (sessions.empty()) {
        // Expected for non-video companion pages: nothing to play.
        CSN_LOG_INFO("MediaController::Play: no media session found (nothing to play).");
        return;
    }
    // Resume only the FIRST session that is Paused; if none is paused (all are
    // already playing or stopped), do nothing. This avoids resuming the wrong
    // tab when several playable pages are open — landing on the right one
    // depends on the user keeping a single playable page open (or luck).
    for (const auto& s : sessions) {
        try {
            PlaybackStatus st = MapStatus(s.GetPlaybackInfo().PlaybackStatus());
            if (st == PlaybackStatus::Paused) {
                s.TryPlayAsync().get();
                CSN_LOG_INFO("MediaController: sent Play (was Paused).");
                return;
            }
        } catch (const winrt::hresult_error& e) {
            CSN_LOG_WARN("MediaController::Play failed: code " +
                         std::to_string(static_cast<int>(e.code())));
        } catch (...) {
            CSN_LOG_WARN("MediaController::Play threw an exception.");
        }
    }
    CSN_LOG_INFO("MediaController::Play skipped (no paused session found).");
}

void MediaController::LogStatus(const char* context) {
    auto sessions = FindSessions(browser_exe_);
    if (sessions.empty()) {
        CSN_LOG_INFO(std::string(context) + ": no media session found.");
        return;
    }
    try {
        PlaybackStatus st = MapStatus(sessions[0].GetPlaybackInfo().PlaybackStatus());
        CSN_LOG_INFO(std::string(context) + ": " + std::to_string(sessions.size()) +
                     " session(s), first = " + StatusName(st));
    } catch (...) {
        CSN_LOG_INFO(std::string(context) + ": media status query failed.");
    }
}

} // namespace csn
