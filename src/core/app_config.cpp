#include "core/app_config.h"
#include "core/config.h"
#include "core/logger.h"

#include <shlobj.h>
#include <objbase.h>
#include <filesystem>
#include <system_error>

namespace csn {
namespace fs = std::filesystem;

std::filesystem::path AppDataConfigPath() {
    wchar_t* buf = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &buf))) {
        fs::path p(buf);
        CoTaskMemFree(buf);
        p /= "CODMSpawnSnack";
        p /= "config.json";
        return p;
    }
    // Fallback to a local config.json if the shell API is unavailable.
    return "config.json";
}

std::shared_ptr<Config> LoadAppConfig() {
    auto cfg = std::make_shared<Config>();
    const auto p = AppDataConfigPath();
    std::error_code ec;
    if (fs::exists(p, ec)) {
        if (!LoadConfig(p, *cfg)) {
            CSN_LOG_WARN("Failed to parse app config; using built-in defaults.");
        }
        return cfg;
    }
    // First run: persist the built-in defaults so they are visible/editable.
    fs::create_directories(p.parent_path(), ec);
    if (!SaveConfig(p, *cfg)) {
        CSN_LOG_WARN("Could not write default config to " + p.string());
    } else {
        CSN_LOG_INFO("Created default config at " + p.string());
    }
    return cfg;
}

bool SaveAppConfig(const Config& cfg) {
    const auto p = AppDataConfigPath();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return SaveConfig(p, cfg);
}

} // namespace csn
