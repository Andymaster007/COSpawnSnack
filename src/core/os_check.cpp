#include "core/os_check.h"

#include <Windows.h>
#include <string>

namespace csn {

namespace {

constexpr DWORD kMinBuild = 17134; // Windows 10 1803

bool RegReadDword(HKEY root, const wchar_t* subKey, const wchar_t* value, DWORD& out) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = sizeof(DWORD), data = 0;
    bool ok = RegQueryValueExW(hKey, value, nullptr, &type,
                               reinterpret_cast<LPBYTE>(&data), &size) == ERROR_SUCCESS
              && type == REG_DWORD;
    RegCloseKey(hKey);
    if (ok) out = data;
    return ok;
}

bool RegReadString(HKEY root, const wchar_t* subKey, const wchar_t* value, std::wstring& out) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(hKey, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) {
        RegCloseKey(hKey);
        return false;
    }
    std::wstring buf;
    buf.resize(size / sizeof(wchar_t));
    bool ok = RegQueryValueExW(hKey, value, nullptr, &type,
                               reinterpret_cast<LPBYTE>(&buf[0]), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    while (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    if (ok) out = buf;
    return ok;
}

// Fills the NT major version and build number from the registry.
// Returns false only when neither value could be read at all.
bool ReadVersion(DWORD& major, DWORD& build) {
    major = 0;
    build = 0;
    bool hasMajor = RegReadDword(HKEY_LOCAL_MACHINE,
                                 L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                 L"CurrentMajorVersionNumber", major);
    std::wstring buildStr;
    if (RegReadString(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     L"CurrentBuild", buildStr)) {
        build = static_cast<DWORD>(_wtoi(buildStr.c_str()));
    }
    return hasMajor || build != 0;
}

} // namespace

bool IsSupportedOS() {
    DWORD major = 0, build = 0;
    if (!ReadVersion(major, build)) return false;
    if (major < 10) return false; // Windows 7/8/8.1
    return build >= kMinBuild;
}

std::wstring GetOSVersionString() {
    DWORD major = 0, build = 0;
    ReadVersion(major, build);

    const wchar_t* name = L"Windows";
    if (major == 10) name = L"Windows 10/11";
    else if (major == 6 && build >= 9200) name = L"Windows 8/8.1";
    else if (major == 6) name = L"Windows 7";

    std::wstring s = name;
    s += L" build ";
    s += std::to_wstring(build);
    return s;
}

} // namespace csn
