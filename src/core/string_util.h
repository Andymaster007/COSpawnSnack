#pragma once
#include <Windows.h>
#include <string>

namespace csn {

// UTF-8 <-> UTF-16 helpers shared across modules.
inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), size);
    return result;
}

inline std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace csn
