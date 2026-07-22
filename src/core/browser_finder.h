#pragma once
#include <string>

namespace csn {

// Resolve a logical browser name ("chrome" / "edge") to a runnable executable
// path. Prefers well-known install locations; falls back to the bare exe name
// (resolved via PATH) when not found at the expected location.
std::wstring FindBrowserExecutable(const std::string& name);

} // namespace csn
