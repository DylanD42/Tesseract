// The scene list and the per-frame 3D render.
//
// To add a scene: write a `void SceneMyThing(const SceneCtx&)` in scenes.cpp and add a row to kScenes.
// The list is the built-in scenes followed by the imported models (render/model_library.h).
// A scene selector only needs SceneName(i), SceneDescription(i) and SceneCount().
#pragma once
#include "render/model_library.h"
#include "render/scene_helpers.h"
#include "render/viewer.h"

#include <raylib.h>
#include <string>

namespace tess {

struct SceneDef {
    const char* name;
    Color sky;
    Vector3 fog;              // 0..1 rgb
    float fogStart, fogEnd, fogMax;
    Vector3 light;            // direction the light travels
    void (*draw)(const SceneCtx&);
    const char* description;  // shown in the scene selector
};

int SceneCount();                     // built-in + imported
int BuiltInSceneCount();
std::string SceneName(int index);
std::string SceneDescription(int index);   // may be empty (imported scenes without one)
bool IsImportedScene(int index);
int ImportedIndex(int sceneIndex);    // position in the model library (-1 for a built-in scene)

// Everything the renderer needs besides the scene itself.
struct RenderInputs {
    const ViewerRig& rig;
    const ScreenGeometry& geo;
    const SceneShader& shader;
    const SceneTextures& textures;
    const ModelPlayback* playback = nullptr;   // imported scenes: animation clip, frame and spin
};

// Draws one full frame of the given scene with the off-axis projection (ClearBackground included).
void RenderScene(int sceneIndex, const RenderInputs& in);

// The pen-tip test: a gridded box with a red dot penTipMm in front of the screen center.
void RenderPenTipTest(double penTipMm, const RenderInputs& in);

} // namespace tess
