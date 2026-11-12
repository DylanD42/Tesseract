// Procedural textures (made with OpenCV once at startup, uploaded with mipmaps + anisotropic filtering).
#pragma once
#include <raylib.h>

namespace tess {

struct SceneTextures {
    Texture2D grass{}, stone{}, wood{}, rug{}, tiles{};
    void load();
    void unload();
};

} // namespace tess
