#pragma once
#include <string>

namespace csn {

// Resolve a logical browser name ("chrome" / "edge") to a runnable executable
// path. Prefers the chosen browser; if it is not installed, falls back to the
// next adapted browser in priority order (chrome first). Returns an empty
// string when no adapted browser is installed at all.
std::wstring FindBrowserExecutable(const std::string& name);

// True if at least one adapted browser (Chrome / Edge) is installed. Used at
// startup to warn the user before they try to open any web page.
bool AnyAdaptedBrowserInstalled();

// Open `url` in a browser. Prefers the browser named by `preferred` (e.g.
// "chrome"); when empty or not installed, falls back to the first adapted
// browser in priority order (chrome first). Returns true if a browser was
// launched. Used for external links (GitHub / personal homepage).
bool OpenUrlInBrowser(const std::wstring& url, const std::string& preferred = "");

} // namespace csn
