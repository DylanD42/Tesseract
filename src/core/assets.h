// Finds the model files in custom/ regardless of the working directory.
#pragma once
#include <filesystem>
#include <string>

namespace tess {

// Looks for custom/<name> in the working directory, next to the executable, then in the source tree.
// Returns an empty path if the file is nowhere to be found.
std::filesystem::path FindAsset(const std::string& name);

} // namespace tess
