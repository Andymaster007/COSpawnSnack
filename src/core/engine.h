#pragma once
#include "core/config.h"
#include "core/string_util.h"
#include "focus/focus_controller.h"
#include "video/ivideo_target.h"
#include "video/browser_video_target.h"

#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>

namespace csn {

// Owns the live detection loop in a background thread and exposes a tiny
// control surface (Start / Stop / TestSwitch / status callback) for the UI.
//
// The UI and the Engine share the same Config object. The detection worker
// reads ROI/keywords/thresholds at startup; the companion (video) target is
// rebuilt lazily whenever the URL or browser path changes, so UI edits take
// effect without a full restart.
class Engine {
public:
    // monitoring: whether the engine loop is running; window_found: whether
    // the game window has been located yet (false while still waiting for it);
    // ocr_ok: false when the OCR language pack is missing (result detection dead).
    using StatusCallback = std::function<void(bool monitoring, bool window_found, bool ocr_ok)>;

    // Transient user-facing messages (e.g. browser-launch failure) surfaced
    // as toasts by the UI.
    using MessageCallback = std::function<void(const std::string&)>;

    explicit Engine(std::shared_ptr<Config> config);
    ~Engine();

    // Start the background detection loop. If the game window is not present
    // yet, the worker waits (polling once per second) until it appears or
    // Stop() is called. Safe to call only when not already running.
    bool Start();
    // Stop the loop and join the worker thread. Idempotent.
    void Stop();
    bool IsRunning() const { return running_.load(); }
    bool IsWindowFound() const { return window_found_.load(); }

    // Manually trigger one show/hide cycle to verify the browser + URL config.
    // Works independently of Start()/Stop(). Returns true if a cycle actually
    // ran (game window + companion URL present), false otherwise.
    bool TestSwitch();

    void SetStatusCallback(StatusCallback cb) { status_cb_ = std::move(cb); }
    void SetMessageCallback(MessageCallback cb) { msg_cb_ = std::move(cb); }

    // Pushes the current (monitoring, window_found, ocr_ok) state to the UI.
    void NotifyStatus();
    // Pushes a transient message to the UI (toast).
    void NotifyMessage(const std::string& m);

    bool IsOcrOk() const { return ocr_ok_.load(); }

    // UI-facing accessors for the companion (video) settings. Guarded by
    // video_config_mutex_ so concurrent worker reads stay safe.
    void SetCompanion(const std::string& url, const std::string& browser_path);
    std::string GetCompanionUrl() const;
    std::string GetBrowserChoice() const;

private:
    void Worker();
    void EnsureVideoTarget();
    void SwitchToVideo();
    void SwitchBackToGame();

    std::shared_ptr<Config> config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> window_found_{false};
    std::thread thread_;
    StatusCallback status_cb_;
    MessageCallback msg_cb_;
    std::atomic<bool> ocr_ok_{true};

    FocusController focus_;
    std::unique_ptr<IVideoTarget> video_target_;
    std::string video_url_;       // last url used to build video_target_
    std::string video_browser_;   // last browser path used
    HWND game_hwnd_ = nullptr;

    // Guards reads/writes of the companion URL + browser path, which the
    // worker may read while the UI thread writes them.
    mutable std::mutex video_config_mutex_;
};

} // namespace csn
