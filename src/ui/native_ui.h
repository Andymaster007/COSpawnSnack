#pragma once
#include <Windows.h>
#include <memory>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/engine.h"

namespace csn {

// A zero-dependency native Win32 UI, shown only when WebView2 is unavailable
// (e.g. blocked by a code-integrity / WDAC policy that prevents the WebView2
// browser process from starting). It mirrors the essential controls of the
// HTML UI -- companion URL, browser choice, start/stop/test -- and talks to
// the exact same Engine methods the app uses, so behavior is identical.
class NativeUI {
public:
    NativeUI(HWND parent, Engine* engine, std::shared_ptr<Config> config);
    ~NativeUI();

    // Creates the child controls. Returns false on failure.
    bool Create();

    // Live status pushed from the Engine.
    void UpdateStatus(bool monitoring);

    // Forwards a WM_COMMAND from the host window. Returns true if handled.
    bool HandleCommand(WPARAM wParam, LPARAM lParam);

private:
    // Reads the controls and persists them via the Engine + config file.
    void SaveFromControls();

    HWND parent_ = nullptr;
    Engine* engine_ = nullptr;
    std::shared_ptr<Config> config_;

    HWND url_edit_ = nullptr;
    HWND browser_combo_ = nullptr;
    HWND status_static_ = nullptr;

    // Parallel to the combo box items (display name -> logical browser name).
    std::vector<std::string> browser_names_ = {"chrome", "edge"};

    enum Ctl {
        URL_EDIT = 100,
        BROWSER_COMBO = 101,
        START_BTN = 102,
        STOP_BTN = 103,
        TEST_BTN = 104,
        STATUS_STATIC = 105,
    };
};

} // namespace csn
