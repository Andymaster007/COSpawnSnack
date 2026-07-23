#include "ui/native_ui.h"
#include "core/logger.h"
#include "core/app_config.h"
#include "core/browser_finder.h"
#include "core/string_util.h"

#include <commctrl.h>

namespace csn {

NativeUI::NativeUI(HWND parent, Engine* engine, std::shared_ptr<Config> config)
    : parent_(parent), engine_(engine), config_(std::move(config)) {}

NativeUI::~NativeUI() = default;

bool NativeUI::Create() {
    // Ensure common-control classes (EDIT, COMBOBOX, BUTTON, STATIC enhancements)
    // are registered for this process. Harmless if already initialized.
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    DWORD le = 0;

    if (!parent_ || !::IsWindow(parent_)) {
        CSN_LOG_ERROR("NativeUI::Create: parent is not a valid window.");
        return false;
    }
    if (!hinst) {
        CSN_LOG_ERROR("NativeUI::Create: GetModuleHandleW returned null.");
        return false;
    }

    // Companion URL.
    CreateWindowW(L"STATIC",
                  L"伴侣视频网址（死亡时打开，可为抖音 / B站 / 快手 等）:",
                  WS_CHILD | WS_VISIBLE, 20, 18, 820, 22,
                  parent_, nullptr, hinst, nullptr);
    url_edit_ = CreateWindowW(
        L"EDIT", Utf8ToWide(engine_->GetCompanionUrl()).c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        20, 44, 820, 24, parent_, (HMENU)URL_EDIT, hinst, nullptr);
    if (!url_edit_) {
        le = GetLastError();
        CSN_LOG_ERROR("NativeUI: url_edit_ creation failed; GetLastError=" +
                      std::to_string(static_cast<unsigned long>(le)) +
                      " (0x" + std::to_string(le) + ")");
        return false;
    }

    // Browser choice.
    CreateWindowW(L"STATIC", L"浏览器:", WS_CHILD | WS_VISIBLE, 20, 84, 120, 22,
                  parent_, nullptr, hinst, nullptr);
    browser_combo_ = CreateWindowW(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        20, 110, 200, 24, parent_, (HMENU)BROWSER_COMBO, hinst, nullptr);
    if (!browser_combo_) {
        le = GetLastError();
        CSN_LOG_ERROR("NativeUI: browser_combo_ creation failed; last error 0x" +
                      std::to_string(static_cast<unsigned long>(le)));
        return false;
    }
    SendMessageW(browser_combo_, CB_ADDSTRING, 0, (LPARAM)L"Chrome");
    SendMessageW(browser_combo_, CB_ADDSTRING, 0, (LPARAM)L"Edge");
    std::string cur = engine_->GetBrowserChoice();
    int sel = 0;
    for (size_t i = 0; i < browser_names_.size(); ++i) {
        if (browser_names_[i] == cur) { sel = static_cast<int>(i); break; }
    }
    SendMessageW(browser_combo_, CB_SETCURSEL, sel, 0);

    // Action buttons.
    if (!CreateWindowW(L"BUTTON", L"▶ 开始监控", WS_CHILD | WS_VISIBLE,
                  20, 158, 150, 34, parent_, (HMENU)START_BTN, hinst, nullptr)) {
        CSN_LOG_ERROR("NativeUI: START_BTN creation failed.");
        return false;
    }
    if (!CreateWindowW(L"BUTTON", L"■ 停止", WS_CHILD | WS_VISIBLE,
                  185, 158, 150, 34, parent_, (HMENU)STOP_BTN, hinst, nullptr)) {
        CSN_LOG_ERROR("NativeUI: STOP_BTN creation failed.");
        return false;
    }
    if (!CreateWindowW(L"BUTTON", L"测试切换", WS_CHILD | WS_VISIBLE,
                  350, 158, 150, 34, parent_, (HMENU)TEST_BTN, hinst, nullptr)) {
        CSN_LOG_ERROR("NativeUI: TEST_BTN creation failed.");
        return false;
    }

    // Live status.
    status_static_ = CreateWindowW(
        L"STATIC", L"状态: 未运行", WS_CHILD | WS_VISIBLE, 20, 208, 420, 22,
        parent_, (HMENU)STATUS_STATIC, hinst, nullptr);
    if (!status_static_) {
        CSN_LOG_ERROR("NativeUI: status_static_ creation failed.");
        return false;
    }

    // Informational notes.
    CreateWindowW(L"STATIC",
        L"提示：内置原生界面，无需任何额外下载或运行环境。",
        WS_CHILD | WS_VISIBLE, 20, 244, 840, 22, parent_, nullptr, hinst, nullptr);
    CreateWindowW(L"STATIC", L"快捷键：F8 或 Ctrl+F8 可随时 开始 / 停止 监控。",
        WS_CHILD | WS_VISIBLE, 20, 270, 840, 22, parent_, nullptr, hinst, nullptr);

    UpdateStatus(engine_->IsRunning());
    return true;
}

void NativeUI::UpdateStatus(bool monitoring) {
    if (!status_static_) return;
    SetWindowTextW(status_static_,
                   monitoring ? L"状态: 运行中 ▶" : L"状态: 未运行");
}

void NativeUI::SaveFromControls() {
    wchar_t buf[4096] = {};
    GetWindowTextW(url_edit_, buf, 4096);
    std::string url = WideToUtf8(buf);
    int idx = static_cast<int>(SendMessageW(browser_combo_, CB_GETCURSEL, 0, 0));
    std::string browser = (idx >= 0 && static_cast<size_t>(idx) < browser_names_.size())
                              ? browser_names_[idx]
                              : "chrome";
    std::string path = WideToUtf8(FindBrowserExecutable(browser));
    engine_->SetCompanion(url, path);
    SaveAppConfig(*config_);
    CSN_LOG_INFO("Native UI saved config: url=" + url + " browser=" + browser);
}

bool NativeUI::HandleCommand(WPARAM wParam, LPARAM /*lParam*/) {
    int id = LOWORD(wParam);
    switch (id) {
        case START_BTN:
            SaveFromControls();
            engine_->Start();
            UpdateStatus(engine_->IsRunning());
            return true;
        case STOP_BTN:
            engine_->Stop();
            UpdateStatus(engine_->IsRunning());
            return true;
        case TEST_BTN:
            SaveFromControls();
            engine_->TestSwitch();
            return true;
        default:
            return false;  // let DefWindowProc handle combo / edit notifications
    }
}

} // namespace csn
