#include "core/assets.h"

#include <raylib.h>

namespace tess {

std::filesystem::path FindAsset(const std::string& name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path candidates[] = {
        fs::current_path(ec) / "custom" / name,
        fs::path(GetApplicationDirectory()) / "custom" / name,   // CMake copies custom/ next to the exe
#ifdef TESSERACT_SOURCE_ASSET_DIR
        fs::path(TESSERACT_SOURCE_ASSET_DIR) / name,             // running from the IDE or a fresh build tree
#endif
    };
    for (const fs::path& p : candidates)
        if (fs::exists(p, ec)) return p;
    return {};
}

} // namespace tess
