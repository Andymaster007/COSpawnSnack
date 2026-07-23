#pragma once
#include <string>

namespace csn {

// Returns true if the running OS meets the minimum requirement:
// Windows 10 version 1803 (build 17134) or later (this also covers Windows 11).
bool IsSupportedOS();

// Human-readable version description for logging, e.g. "Windows 10/11 build 19045".
// Reads from the same registry source as IsSupportedOS().
std::wstring GetOSVersionString();

} // namespace csn
