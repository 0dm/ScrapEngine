#pragma once

#include <filesystem>

namespace sauce::platform {

/// Absolute path of this process's executable (macOS: via `_NSGetExecutablePath`).
std::filesystem::path executablePath();

/// Walks upward from the executable directory looking for a folder that contains both
/// `testScene/` and `assets/` (SauceEngine repo root). Returns empty if not found.
std::filesystem::path findProjectRootNearExecutable();

} // namespace sauce::platform
