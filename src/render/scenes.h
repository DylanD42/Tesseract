// The scene list and the per-frame 3D render.
//
// To add a scene: write a `void SceneMyThing(const SceneCtx&)` in scenes.cpp and add a row to kScenes.
// A scene selector only needs Scenes()[i].name and SceneCount().
#pragma once
#include "render/scene_helpers.h"
#include "render/viewer.h"

#include <raylib.h>

namespace tess {

struct SceneDef {
    const char* name;
    Color sky;
    Vector3 fog;              // 0..1 rgb
    float fogStart, fogEnd, fogMax;
    Vector3 light;            // direction the light travels
    void (*draw)(const SceneCtx&);
};

const SceneDef* Scenes();
int SceneCount();

// Everything the renderer needs besides the scene itself.
struct RenderInputs {
    const ViewerRig& rig;
    const ScreenGeometry& geo;
    const SceneShader& shader;
    const SceneTextures& textures;
};

// Draws one full frame of the given scene with the off-axis projection (ClearBackground included).
void RenderScene(int sceneIndex, const RenderInputs& in);

// The pen-tip test: a gridded box with a red dot penTipMm in front of the screen center.
void RenderPenTipTest(double penTipMm, const RenderInputs& in);

} // namespace tess
