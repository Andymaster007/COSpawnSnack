#pragma once
#include <string>
#include <Windows.h>

namespace csn {

class FocusController {
public:
    HWND FindWindowByTitle(const std::wstring& title_substring);
    bool SwitchToWindow(HWND hwnd);
};

} // namespace csn
