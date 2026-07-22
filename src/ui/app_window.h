#pragma once
#include <Windows.h>
#include <memory>

#include "core/config.h"
#include "core/engine.h"
#include "ui/webview2_controller.h"

namespace csn {

// The single visible window. Hosts the WebView2 UI (with a native Win32
// fallback), registers the F8 global hotkey, and owns the Engine lifecycle
// (stop on close).
class AppWindow {
public:
    AppWindow(std::shared_ptr<Engine> engine, std::shared_ptr<Config> config);
    ~AppWindow();

    bool Create();
    void RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    std::shared_ptr<Engine> engine_;
    std::shared_ptr<Config> config_;
    std::unique_ptr<WebUI> webui_;
};

} // namespace csn
