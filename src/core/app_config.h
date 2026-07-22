#pragma once
#include "core/config.h"
#include <memory>
#include <filesystem>

namespace csn {

// Returns the per-user config path: %APPDATA%/CODMSpawnSnack/config.json.
std::filesystem::path AppDataConfigPath();

// Loads the config from %APPDATA%. If the file does not exist yet, writes the
// built-in defaults there and returns them. If it exists but is unreadable,
// returns defaults without overwriting. The Config object is shared with the
// Engine so UI edits are visible to the detection loop.
std::shared_ptr<Config> LoadAppConfig();

// Persists the current config back to %APPDATA% (creating the directory if
// needed). Called whenever the UI changes an option.
bool SaveAppConfig(const Config& cfg);

} // namespace csn
