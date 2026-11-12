// Drawing helpers shared by the scenes. Everything is in world units (see core/config.h).
#pragma once
#include "render/scene_shader.h"
#include "render/scene_textures.h"

#include <raylib.h>

namespace tess {

// What a scene gets each frame.
struct SceneCtx {
    float L, R, B, T, zG;       // screen opening in world units; glass plane z
    double time;                // seconds since start (animations)
    const SceneShader* sh;
    const SceneTextures* tex;
    Vector3 lightDir;           // normalized direction the light travels
    float unitsPerMm;           // world units per physical millimetre (for real-size objects)
};

void Lit(const SceneCtx& c, bool on);
void Matte(const SceneCtx& c);   // lit, no specular glare (ground, rugs)

void DrawQuad3D(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col);
void TexQuad(const Texture2D& tex, Vector3 a, Vector3 b, Vector3 c, Vector3 d, float u, float v, Color tint = WHITE);
// Horizontal textured rectangle at height y, tile = world units per texture repeat.
void TexFloor(const Texture2D& tex, float x0, float x1, float z0, float z1, float y, float tile, Color tint = WHITE);
void Box(float x0, float x1, float y0, float y1, float z0, float z1, Color c);

// The window's "reveal": the thickness of the wall around the opening. Moving your head shows its inner faces,
// and it hides and reveals the world behind it: the strongest cue that the screen is an opening.
void DrawReveal(const SceneCtx& c, float depth, Color sides, Color head, Color sill);
void DrawGlassEdge(const SceneCtx& c, Color col);

// Soft blob shadow on a floor at floorY, cast along the light direction from a point at `p`.
void DropShadow(Vector3 p, float size, float floorY, Vector3 light, float lift, float strength = 1.0f);
void BeginShadows();
void EndShadows();

void DrawGrid(float L, float R, float B, float T, float zF, float zB, float ringStep, float lineStep, Color c);
void DrawBoxRoom(float L, float R, float B, float T, float zF, float zB, Color floor, Color walls, Color ceil, Color back);
void DrawPine(float x, float z, float groundY, float h, Color leaves);
void DrawRoundTree(float x, float z, float groundY, float h, Color leaves);

} // namespace tess
