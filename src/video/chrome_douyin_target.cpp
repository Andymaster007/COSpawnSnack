#include "video/chrome_douyin_target.h"
#include "core/logger.h"

#include <shellapi.h>
#include <string>

namespace csn {

namespace {
    constexpr const wchar_t* kChromePath = L"chrome.exe";
    constexpr const wchar_t* kDouyinUrl = L"https://www.douyin.com";
}

HWND ChromeDouyinTarget::FindTargetWindow() const {
    // First try exact "抖音" title.
    HWND hwnd = FindWindowW(nullptr, L"抖音");
    if (hwnd) return hwnd;

    // Otherwise any visible Chrome-ish window with douyin in the title.
    struct EnumCtx { HWND found = nullptr; } ctx;
    EnumWindows([](HWND h, LPARAM lParam) -> BOOL {
        wchar_t buf[256]{};
        if (GetWindowTextW(h, buf, 256) > 0) {
            std::wstring title(buf);
            if ((title.find(L"抖音") != std::wstring::npos ||
                 title.find(L"douyin") != std::wstring::npos) &&
                IsWindowVisible(h)) {
                reinterpret_cast<EnumCtx*>(lParam)->found = h;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

bool ChromeDouyinTarget::SendSpaceKey(HWND hwnd) {
    if (!hwnd) return false;
    SetForegroundWindow(hwnd);
    INPUT input[2] = {};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_SPACE;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = VK_SPACE;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    UINT sent = SendInput(2, input, sizeof(INPUT));
    return sent == 2;
}

bool ChromeDouyinTarget::Launch() {
    HWND hwnd = FindTargetWindow();
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        running_ = true;
        return true;
    }

    std::wstring args = std::wstring(L"--app=") + kDouyinUrl;
    HINSTANCE result = ShellExecuteW(nullptr, L"open", kChromePath, args.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        CSN_LOG_ERROR("Failed to launch Chrome for Douyin.");
        return false;
    }
    running_ = true;
    return true;
}

bool ChromeDouyinTarget::Pause() {
    HWND hwnd = FindTargetWindow();
    if (!hwnd) return false;
    return SendSpaceKey(hwnd);
}

bool ChromeDouyinTarget::Resume() {
    HWND hwnd = FindTargetWindow();
    if (!hwnd) return false;
    return SendSpaceKey(hwnd);
}

bool ChromeDouyinTarget::IsRunning() const {
    return running_ && FindTargetWindow() != nullptr;
}

} // namespace csn
