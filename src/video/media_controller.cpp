#include "video/media_controller.h"
#include "core/logger.h"

#include <windows.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <nlohmann/json.hpp>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace csn {

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Foundation::Collections;

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
    return s;
}

// ---------------------------------------------------------------------------
// GSMTC helpers (fallback path only)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// WinHTTP helpers for CDP
// ---------------------------------------------------------------------------

// GET a plain HTTP resource (used for /json). Returns the body, or empty on
// any failure.
std::string WinHttpGet(const std::wstring& host, int port, const std::wstring& path) {
    std::string out;
    HINTERNET hSession = WinHttpOpen(L"csn/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return out;
    // Bound all phases so we never hang on a dead port.
    WinHttpSetTimeouts(hSession, 1500, 1500, 1500, 1500);
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return out; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return out; }
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return out;
    }
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return out;
    }
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
        std::vector<char> buf(avail + 1, 0);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf.data(), avail, &read) || read == 0) break;
        out.append(buf.data(), read);
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return out;
}

// Open a CDP WebSocket (ws://) to the given target URL and send a single
// Runtime.evaluate with the supplied expression. Returns true if the upgrade
// and send succeeded. Never throws (called from detached control threads).
bool CdpSend(const std::string& wsUrl, const std::string& expression) {
    try {
        // Parse ws://host:port/path
        std::string s = wsUrl;
        if (s.rfind("ws://", 0) == 0) s = s.substr(5);
        size_t slash = s.find('/');
        std::string authority = (slash == std::string::npos) ? s : s.substr(0, slash);
        std::string path = (slash == std::string::npos) ? std::string("/") : s.substr(slash);
        size_t colon = authority.rfind(':');
        std::string host = (colon == std::string::npos) ? authority : authority.substr(0, colon);
        int port = (colon == std::string::npos) ? 9222 : std::stoi(authority.substr(colon + 1));

        std::wstring whost(host.begin(), host.end());
        std::wstring wpath(path.begin(), path.end());

        HINTERNET hSession = WinHttpOpen(L"csn/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        WinHttpSetTimeouts(hSession, 1500, 1500, 1500, 1500);
        HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), (INTERNET_PORT)port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), nullptr,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return false;
        }
        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return false;
        }
        HINTERNET hWs = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
        if (!hWs) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return false;
        }

        nlohmann::json cmd;
        cmd["id"] = 1;
        cmd["method"] = "Runtime.evaluate";
        cmd["params"] = {{"expression", expression}, {"returnByValue", false}};
        std::string payload = cmd.dump();

        BOOL sent = WinHttpWebSocketSend(hWs, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                         (PVOID)payload.data(), (DWORD)payload.size());

        // Best-effort drain: read one frame so we know the command was delivered
        // before we tear the socket down. Ignore content/errors.
        std::vector<BYTE> rbuf(4096);
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        DWORD rread = 0;
        WinHttpWebSocketReceive(hWs, rbuf.data(), (DWORD)rbuf.size(), &rread, &type);

        WinHttpWebSocketClose(hWs, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        WinHttpCloseHandle(hWs);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return sent == TRUE;
    } catch (...) {
        return false;
    }
}

} // namespace

MediaController::MediaController(std::wstring browser_exe, int cdp_port,
                                 std::string video_host)
    : browser_exe_(ToLower(std::move(browser_exe))),
      cdp_port_(cdp_port),
      video_host_(std::move(video_host)) {}

MediaController::~MediaController() = default;

void MediaController::Pause() {
    if (CdpBroadcast(/*play=*/false)) return;
    GsmtcPause();
}

void MediaController::Play() {
    if (CdpBroadcast(/*play=*/true)) return;
    GsmtcPlay();
}

void MediaController::LogStatus(const char* context) {
    std::wstring host = L"127.0.0.1";
    std::string body = WinHttpGet(host, cdp_port_, L"/json");
    if (body.empty()) {
        CSN_LOG_INFO(std::string(context) + ": CDP unavailable (port " +
                     std::to_string(cdp_port_) + "), GSMTC fallback active.");
        GsmtcLogStatus();
        return;
    }
    try {
        auto list = nlohmann::json::parse(body);
        int pages = 0;
        if (list.is_array()) {
            for (auto& t : list) {
                if (t.is_object() && t.value("type", std::string()) == "page") ++pages;
            }
        }
        CSN_LOG_INFO(std::string(context) + ": CDP reachable, " +
                     std::to_string(pages) + " page tab(s), video_host='" +
                     video_host_ + "'.");
    } catch (...) {
        CSN_LOG_INFO(std::string(context) + ": CDP reachable but /json parse failed.");
    }
}

bool MediaController::CdpBroadcast(bool play) {
    std::wstring host = L"127.0.0.1";
    std::string body = WinHttpGet(host, cdp_port_, L"/json");
    if (body.empty()) {
        CSN_LOG_WARN("MediaController: CDP /json unreachable on port " +
                     std::to_string(cdp_port_) + ", falling back to GSMTC.");
        return false;
    }
    nlohmann::json list;
    try {
        list = nlohmann::json::parse(body);
    } catch (...) {
        CSN_LOG_WARN("MediaController: CDP /json parse failed, falling back to GSMTC.");
        return false;
    }
    if (!list.is_array()) {
        CSN_LOG_WARN("MediaController: CDP /json is not an array, falling back to GSMTC.");
        return false;
    }

    // video.pause()/play() injected into every matching page tab. The IIFE is
    // idempotent: an absent <video> is skipped, an already-paused/playing one
    // is a no-op. play()'s promise is swallowed so autoplay rejection can't
    // surface as an unhandled rejection in the page.
    const char* kPlayExpr =
        "(()=>{const v=document.querySelector('video');if(v){try{const p=v.play();"
        "if(p&&p.catch)p.catch(()=>{});}catch(e){}}})()";
    const char* kPauseExpr =
        "(()=>{const v=document.querySelector('video');if(v){try{v.pause();}catch(e){}}})()";
    const char* expr = play ? kPlayExpr : kPauseExpr;

    std::string want = ToLowerAscii(video_host_);
    int targeted = 0;
    int acted = 0;
    for (auto& t : list) {
        if (!t.is_object()) continue;
        if (t.value("type", std::string()) != "page") continue;
        std::string url = t.value("url", std::string());
        if (!want.empty() && ToLowerAscii(url).find(want) == std::string::npos) {
            continue;
        }
        std::string ws = t.value("webSocketDebuggerUrl", std::string());
        if (ws.empty()) continue;
        ++targeted;
        if (CdpSend(ws, expr)) ++acted;
    }
    CSN_LOG_INFO("MediaController: CDP " + std::string(play ? "Play" : "Pause") +
                 " targeted " + std::to_string(targeted) + " tab(s), acted on " +
                 std::to_string(acted) + ".");
    return true; // CDP was reachable; even with 0 tabs we own control.
}

// ---------------------------------------------------------------------------
// GSMTC fallback (unchanged behavior, used only when CDP is unreachable)
// ---------------------------------------------------------------------------

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
