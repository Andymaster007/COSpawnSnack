#include "core/browser_finder.h"

#include <Windows.h>
#include <filesystem>
#include <shellapi.h>
#include <string>

namespace csn {
namespace fs = std::filesystem;

namespace {

// Adapted browsers, ordered by preference. The user asked external links to
// prefer Chrome, so Chrome is listed first. More browsers can be appended here
// later (e.g. Brave / Firefox) without touching call sites.
struct Entry {
    const char* key;        // logical name used by the UI / config
    const wchar_t* exe;      // bare executable name (for logging / media lookup)
    const wchar_t* paths[2]; // well-known install locations
};

const Entry kCatalog[] = {
    {"chrome", L"chrome.exe", {
        L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        L"C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe"}},
    {"edge", L"msedge.exe", {
        L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe"}},
};

bool FileExists(const std::wstring& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// Returns the installed absolute path for `choice` (a key like "chrome"/"edge"
// or empty). Pass 1 honours the explicit choice; pass 2 falls back to the
// first installed adapted browser in catalog order. Empty if none installed.
std::wstring Resolve(const std::string& choice) {
    if (!choice.empty()) {
        for (const auto& e : kCatalog) {
            if (e.key == choice) {
                for (const auto& p : e.paths) {
                    if (FileExists(p)) return p;
                }
            }
        }
    }
    for (const auto& e : kCatalog) {
        for (const auto& p : e.paths) {
            if (FileExists(p)) return p;
        }
    }
    return {};
}

} // namespace

std::wstring FindBrowserExecutable(const std::string& name) {
    return Resolve(name);
}

bool AnyAdaptedBrowserInstalled() {
    for (const auto& e : kCatalog) {
        for (const auto& p : e.paths) {
            if (FileExists(p)) return true;
        }
    }
    return false;
}

bool OpenUrlInBrowser(const std::wstring& url, const std::string& preferred) {
    std::wstring exe = Resolve(preferred);
    if (exe.empty()) return false;
    HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(),
                                url.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(r) > 32;
}

} // namespace csn
