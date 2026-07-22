#include "core/browser_finder.h"

#include <filesystem>

namespace csn {
namespace fs = std::filesystem;

static bool FileExists(const std::wstring& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

std::wstring FindBrowserExecutable(const std::string& name) {
    if (name == "edge") {
        const std::wstring candidates[] = {
            L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
            L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        };
        for (const auto& c : candidates) {
            if (FileExists(c)) return c;
        }
        return L"msedge.exe";
    }
    // Default: Chrome.
    const std::wstring candidates[] = {
        L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        L"C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
    };
    for (const auto& c : candidates) {
        if (FileExists(c)) return c;
    }
    return L"chrome.exe";
}

} // namespace csn
