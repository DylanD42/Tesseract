// Lighting without mesh normals (normals rebuilt from screen-space derivatives), optional texture, distance fog.
#pragma once
#include <raylib.h>

namespace tess {

struct SceneShader {
    Shader sh{};
    bool ok = false;
    int viewPos = -1, lightDir = -1, fogColor = -1, fogNear = -1, fogFar = -1, fogMax = -1, lit = -1, shine = -1;

    void load();
    void unload();

    // lit: shaded vs fog-only; shineAmount: specular strength. Uniforms are not batched, so geometry drawn
    // with the previous values is flushed first.
    void setMaterial(bool on, float shineAmount) const;
    void setLit(bool on) const { setMaterial(on, 1.0f); }
};

} // namespace tess
