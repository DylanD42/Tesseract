#include "render/scene_helpers.h"

#include <rlgl.h>
#include <algorithm>
#include <cmath>

namespace tess {

void Lit(const SceneCtx& c, bool on) { c.sh->setLit(on); }
void Matte(const SceneCtx& c) { c.sh->setMaterial(true, 0.0f); }

void DrawQuad3D(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
}

void TexQuad(const Texture2D& tex, Vector3 a, Vector3 b, Vector3 c, Vector3 d, float u, float v, Color tint) {
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlTexCoord2f(0, 0); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(u, 0); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(u, v); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(0, v); rlVertex3f(d.x, d.y, d.z);
    rlEnd();
    rlSetTexture(rlGetTextureIdDefault());   // rlSetTexture(0) alone would leave this texture bound for later quads
}

void TexFloor(const Texture2D& tex, float x0, float x1, float z0, float z1, float y, float tile, Color tint) {
    TexQuad(tex, {x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, (x1 - x0) / tile, (z0 - z1) / tile, tint);
}

void Box(float x0, float x1, float y0, float y1, float z0, float z1, Color c) {
    DrawCube({(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f}, std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0), c);
}

void DrawReveal(const SceneCtx& c, float depth, Color sides, Color head, Color sill) {
    const float z0 = c.zG, z1 = c.zG - depth;
    DrawQuad3D({c.L, c.B, z0}, {c.L, c.B, z1}, {c.L, c.T, z1}, {c.L, c.T, z0}, sides);
    DrawQuad3D({c.R, c.B, z0}, {c.R, c.T, z0}, {c.R, c.T, z1}, {c.R, c.B, z1}, sides);
    DrawQuad3D({c.L, c.T, z0}, {c.L, c.T, z1}, {c.R, c.T, z1}, {c.R, c.T, z0}, head);
    DrawQuad3D({c.L, c.B, z0}, {c.R, c.B, z0}, {c.R, c.B, z1}, {c.L, c.B, z1}, sill);
}

void DrawGlassEdge(const SceneCtx& c, Color col) {
    DrawLine3D({c.L, c.B, c.zG}, {c.R, c.B, c.zG}, col);
    DrawLine3D({c.R, c.B, c.zG}, {c.R, c.T, c.zG}, col);
    DrawLine3D({c.R, c.T, c.zG}, {c.L, c.T, c.zG}, col);
    DrawLine3D({c.L, c.T, c.zG}, {c.L, c.B, c.zG}, col);
}

namespace {
void DrawShadowDisc(Vector3 center, float r, Color col) {
    constexpr int kSeg = 28;
    for (int i = 0; i < kSeg; ++i) {
        const float a0 = 2.0f * PI * i / kSeg, a1 = 2.0f * PI * (i + 1) / kSeg;
        DrawTriangle3D(center, {center.x + r * std::cos(a1), center.y, center.z + r * std::sin(a1)},
                               {center.x + r * std::cos(a0), center.y, center.z + r * std::sin(a0)}, col);
    }
}
} // namespace

void DropShadow(Vector3 p, float size, float floorY, Vector3 light, float lift, float strength) {
    const float h = p.y - floorY;
    if (h < 0 || light.y >= -0.1f) return;
    const float t = h / -light.y;
    const Vector3 c{p.x + light.x * t, floorY + lift, p.z + light.z * t};
    const float r = size * (1.0f + h * 0.02f);
    const float fade = strength * std::clamp(1.0f - h / (size * 30.0f), 0.35f, 1.0f);
    DrawShadowDisc(c, r * 1.30f, Fade(BLACK, 0.07f * fade));
    DrawShadowDisc(c, r * 1.00f, Fade(BLACK, 0.10f * fade));
    DrawShadowDisc(c, r * 0.65f, Fade(BLACK, 0.13f * fade));
}

void BeginShadows() { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
void EndShadows()   { rlDrawRenderBatchActive(); rlEnableDepthMask(); }

void DrawGrid(float L, float R, float B, float T, float zF, float zB, float ringStep, float lineStep, Color c) {
    const float e = 0.02f;
    const int rings = std::max(1, int(std::lround((zF - zB) / ringStep)));
    for (int i = 1; i <= rings; ++i) {
        const float z = zF + (zB - zF) * float(i) / float(rings) + (i == rings ? e : 0.0f);
        DrawLine3D({L + e, B + e, z}, {R - e, B + e, z}, c);
        DrawLine3D({L + e, T - e, z}, {R - e, T - e, z}, c);
        DrawLine3D({L + e, B + e, z}, {L + e, T - e, z}, c);
        DrawLine3D({R - e, B + e, z}, {R - e, T - e, z}, c);
    }
    const int nx = std::max(2, int(std::lround((R - L) / lineStep)));
    for (int i = 1; i < nx; ++i) {
        const float x = L + (R - L) * float(i) / float(nx);
        DrawLine3D({x, B + e, zF}, {x, B + e, zB}, c);
        DrawLine3D({x, T - e, zF}, {x, T - e, zB}, c);
        DrawLine3D({x, B, zB + e}, {x, T, zB + e}, c);
    }
    const int ny = std::max(2, int(std::lround((T - B) / lineStep)));
    for (int j = 1; j < ny; ++j) {
        const float y = B + (T - B) * float(j) / float(ny);
        DrawLine3D({L + e, y, zF}, {L + e, y, zB}, c);
        DrawLine3D({R - e, y, zF}, {R - e, y, zB}, c);
        DrawLine3D({L, y, zB + e}, {R, y, zB + e}, c);
    }
}

void DrawBoxRoom(float L, float R, float B, float T, float zF, float zB, Color floor, Color walls, Color ceil, Color back) {
    DrawQuad3D({L, B, zF}, {R, B, zF}, {R, B, zB}, {L, B, zB}, floor);
    DrawQuad3D({L, T, zF}, {L, T, zB}, {R, T, zB}, {R, T, zF}, ceil);
    DrawQuad3D({L, B, zF}, {L, B, zB}, {L, T, zB}, {L, T, zF}, walls);
    DrawQuad3D({R, B, zF}, {R, T, zF}, {R, T, zB}, {R, B, zB}, walls);
    DrawQuad3D({L, B, zB}, {R, B, zB}, {R, T, zB}, {L, T, zB}, back);
}

void DrawPine(float x, float z, float groundY, float h, Color leaves) {
    DrawCylinder({x, groundY, z}, h * 0.035f, h * 0.05f, h * 0.35f, 10, Color{90, 62, 40, 255});
    for (int i = 0; i < 3; ++i) {
        const float y = groundY + h * (0.22f + 0.24f * i);
        const float r = h * (0.30f - 0.07f * i);
        DrawCylinder({x, y, z}, 0.0f, r, h * 0.42f, 14, leaves);
    }
}

void DrawRoundTree(float x, float z, float groundY, float h, Color leaves) {
    DrawCylinder({x, groundY, z}, h * 0.04f, h * 0.06f, h * 0.55f, 10, Color{96, 66, 44, 255});
    const float r = h * 0.28f;
    DrawSphereEx({x, groundY + h * 0.68f, z}, r, 10, 12, leaves);
    DrawSphereEx({x - r * 0.6f, groundY + h * 0.58f, z + r * 0.2f}, r * 0.75f, 8, 10, leaves);
    DrawSphereEx({x + r * 0.55f, groundY + h * 0.62f, z - r * 0.25f}, r * 0.8f, 8, 10, leaves);
}

} // namespace tess
