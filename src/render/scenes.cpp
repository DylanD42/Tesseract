#include "render/scenes.h"

#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <utility>
#include <vector>

namespace tess {
namespace {

// 1. WINDOW: a real window (reveal, sash, mullions) onto a garden that is far wider than the screen.
//    Near flower box + mullions vs. far trees and hills = huge relative motion between depth layers.
void SceneWindow(const SceneCtx& c) {
    const float W = c.R - c.L, H = c.T - c.B;
    const float rd = 2.2f, zW = c.zG - rd;              // the glass of the real window sits at the back of the reveal
    const float groundY = c.B - 9.0f;
    const Vector3 L = c.lightDir;

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawReveal(c, rd, Color{232, 226, 214, 255}, Color{220, 214, 202, 255}, Color{196, 160, 118, 255});
    // Sash frame and mullions (window bars) at the back of the reveal.
    const Color sash{244, 244, 238, 255};
    const float fw = 0.45f, fd = 0.5f;
    Box(c.L, c.L + fw, c.B, c.T, zW, zW - fd, sash);
    Box(c.R - fw, c.R, c.B, c.T, zW, zW - fd, sash);
    Box(c.L, c.R, c.T - fw, c.T, zW, zW - fd, sash);
    Box(c.L, c.R, c.B, c.B + fw * 1.4f, zW, zW - fd, sash);
    Box(-0.18f, 0.18f, c.B, c.T, zW, zW - fd, sash);
    const float barY = c.B + H * 0.62f;
    Box(c.L, c.R, barY - 0.16f, barY + 0.16f, zW, zW - fd, sash);

    const float zOut = zW - fd - 0.2f;
    // Ground, path, flower box right outside the window.
    Matte(c);
    TexFloor(c.tex->grass, -2500, 2500, zOut, -4000, groundY, 22.0f);
    TexFloor(c.tex->stone, -5.5f, 5.5f, zOut - 14.0f, -420.0f, groundY + 0.25f, 11.0f);
    Lit(c, true);
    Box(-W * 0.42f, W * 0.42f, c.B - 3.4f, c.B - 0.7f, zOut - 0.4f, zOut - 4.0f, Color{170, 92, 60, 255});
    Box(-W * 0.40f, W * 0.40f, c.B - 0.75f, c.B - 0.6f, zOut - 0.6f, zOut - 3.8f, Color{70, 50, 36, 255});
    const Color flowers[] = {Color{230, 60, 80, 255}, Color{250, 200, 40, 255}, Color{240, 240, 245, 255},
                             Color{170, 80, 200, 255}, Color{250, 130, 50, 255}};
    const int nFlowers = std::max(6, int(W / 1.4f));
    for (int i = 0; i < nFlowers; ++i) {
        const float x = -W * 0.38f + (W * 0.76f) * float(i) / float(nFlowers - 1);
        const float z = zOut - 1.2f - 1.4f * float((i * 7) % 3) / 2.0f;
        const float h = 0.9f + 0.5f * float((i * 5) % 4) / 3.0f;
        DrawSphereEx({x, c.B - 0.4f + h, z}, 0.42f, 8, 10, flowers[i % 5]);
        DrawSphereEx({x + 0.35f, c.B - 0.6f + h * 0.6f, z - 0.3f}, 0.35f, 6, 8, Color{60, 140, 60, 255});
    }
    // Hedges either side of the path.
    for (float sx : {-1.0f, 1.0f}) Box(sx * 9.0f, sx * 220.0f, groundY, groundY + 6.0f, -38.0f, -44.0f, Color{46, 112, 58, 255});
    // Fence.
    const float fz = -110.0f;
    for (float x = -300; x <= 300; x += 14) {
        if (std::abs(x) < 6) continue;
        Box(x - 0.45f, x + 0.45f, groundY, groundY + 9.0f, fz + 0.45f, fz - 0.45f, Color{235, 232, 222, 255});
    }
    for (float y : {3.0f, 7.0f}) {
        Box(-300, -6, groundY + y - 0.3f, groundY + y + 0.3f, fz + 0.2f, fz - 0.2f, Color{230, 226, 214, 255});
        Box(6, 300, groundY + y - 0.3f, groundY + y + 0.3f, fz + 0.2f, fz - 0.2f, Color{230, 226, 214, 255});
    }
    // Lamp post close to the window: a tall near vertical that slides across the far scenery.
    const float lx = W * 0.32f + 6.0f, lz = -26.0f;
    DrawCylinder({lx, groundY, lz}, 0.35f, 0.45f, 34.0f, 12, Color{40, 44, 48, 255});
    Box(lx - 2.6f, lx + 0.2f, groundY + 33.6f, groundY + 34.2f, lz + 0.2f, lz - 0.2f, Color{40, 44, 48, 255});
    DrawCylinder({lx - 2.4f, groundY + 31.0f, lz}, 0.6f, 1.4f, 2.6f, 12, Color{40, 44, 48, 255});
    // Bench.
    Box(-34, -20, groundY + 4.0f, groundY + 4.6f, -92, -96, Color{140, 96, 60, 255});
    Box(-34, -20, groundY + 4.6f, groundY + 8.6f, -95.6f, -96.2f, Color{140, 96, 60, 255});
    for (float x : {-33.0f, -21.0f}) Box(x - 0.4f, x + 0.4f, groundY, groundY + 4.0f, -92.5f, -95.5f, Color{50, 50, 55, 255});
    // Trees at many depths.
    struct T3 { float x, z, h; bool pine; };
    static const T3 trees[] = {
        {-40, -70, 42, false}, {48, -88, 50, true}, {-95, -150, 60, true}, {120, -170, 55, false}, {-20, -240, 70, true},
        {-200, -260, 65, false}, {210, -300, 80, true}, {60, -380, 75, false}, {-140, -420, 90, true}, {320, -520, 95, false},
        {-330, -600, 100, true}, {20, -700, 110, true}, {-60, -900, 120, false}, {450, -950, 130, true}, {-520, -1100, 140, true},
        {180, -1300, 150, false}};
    for (const auto& t : trees)
        t.pine ? DrawPine(t.x, t.z, groundY, t.h, Color{38, 104, 60, 255}) : DrawRoundTree(t.x, t.z, groundY, t.h, Color{70, 140, 60, 255});
    // Hills and clouds far away (fog blends them into the sky).
    DrawCylinder({-900, groundY, -2600}, 0.0f, 900, 520, 24, Color{90, 120, 110, 255});
    DrawCylinder({600, groundY, -3000}, 0.0f, 1100, 700, 24, Color{100, 128, 125, 255});
    DrawCylinder({1800, groundY, -2400}, 0.0f, 700, 380, 24, Color{95, 125, 112, 255});
    for (int i = 0; i < 7; ++i) {
        const float x = -900.0f + 320.0f * i, y = groundY + 260.0f + 40.0f * float(i % 3), z = -1500.0f - 150.0f * float(i % 2);
        for (int k = 0; k < 4; ++k)
            DrawSphereEx({x + 45.0f * k, y + 12.0f * float(k % 2), z}, 45.0f + 10.0f * float(k % 2), 8, 10, Color{250, 250, 252, 255});
    }

    Lit(c, false);
    DrawGlassEdge(c, Color{30, 30, 30, 255});
    BeginShadows();
    for (const auto& t : trees) DropShadow({t.x, groundY + t.h * 0.6f, t.z}, t.h * 0.22f, groundY, L, 0.3f, 1.2f);
    DropShadow({lx, groundY + 20.0f, lz}, 2.0f, groundY, L, 0.3f);
    DropShadow({-27, groundY + 4.0f, -94}, 6.0f, groundY, L, 0.3f);
    EndShadows();
}

// 2. TARGETS: Johnny Lee's classic Wii head-tracking demo. A deep gridded box with targets on sticks;
//    a few targets float in front of the glass (kept near the center so the screen edge never cuts them).
void SceneTargets(const SceneCtx& c) {
    const float zB = c.zG - 38.0f;
    struct Tg { float x, y, z, r; };
    static const Tg targets[] = {
        {-4.5f, 2.6f, -4.0f, 1.3f}, {3.8f, -1.8f, -10.0f, 1.5f}, {-1.5f, -2.6f, -18.0f, 1.6f}, {5.0f, 3.2f, -22.0f, 1.4f},
        {-6.0f, -0.8f, -28.0f, 1.8f}, {1.2f, 3.8f, -32.0f, 1.6f}, {0.6f, 0.4f, 4.5f, 0.8f}, {-1.6f, 1.8f, 2.8f, 0.7f},
        {2.2f, 1.4f, -1.0f, 1.0f}, {-3.2f, 4.2f, -14.0f, 1.2f}};

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawBoxRoom(c.L, c.R, c.B, c.T, c.zG, zB, Color{18, 18, 24, 255}, Color{14, 14, 20, 255}, Color{16, 16, 22, 255},
                Color{12, 12, 18, 255});
    for (const auto& t : targets) {
        const Color ring[] = {Color{220, 30, 40, 255}, WHITE, Color{220, 30, 40, 255}, WHITE, Color{220, 30, 40, 255}};
        for (int k = 0; k < 5; ++k) {
            const float rad = t.r * (1.0f - 0.19f * k), z = t.z + 0.03f * k;
            DrawCylinderEx({t.x, t.y, z}, {t.x, t.y, z + 0.12f}, rad, rad, 40, ring[k]);
        }
    }
    Lit(c, false);
    DrawGrid(c.L, c.R, c.B, c.T, c.zG, zB, 3.0f, 1.5f, Color{120, 180, 255, 255});
    for (const auto& t : targets) DrawLine3D({t.x, t.y, t.z}, {t.x, t.y, zB + 0.02f}, Color{200, 200, 210, 255});
    DrawGlassEdge(c, Color{200, 220, 255, 255});
}

// 3. ROOM: a furnished room much larger than the opening, seen through a doorway-like reveal. Familiar-size
//    objects (table, chairs, shelves, lamp) let your brain judge distance; edges of the opening hide and reveal them.
void SceneRoom(const SceneCtx& c) {
    const float W = c.R - c.L;
    const float rd = 1.2f, zF = c.zG - rd, zBk = c.zG - 120.0f;
    const float x0 = c.L - W * 1.3f, x1 = c.R + W * 1.3f;
    const float floorY = c.B - 7.0f, ceilY = c.T + 16.0f;
    const Vector3 L = c.lightDir;
    const Color wall{214, 196, 172, 255}, backWall{205, 186, 162, 255}, trim{92, 64, 44, 255};

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawReveal(c, rd, Color{110, 78, 54, 255}, Color{100, 70, 48, 255}, Color{120, 86, 60, 255});
    // Doorway in the back wall leading into a hallway: real depth beyond the room.
    const float dx = -26.0f, dHalf = 8.5f, dTop = floorY + 30.0f, hallEnd = zBk - 45.0f;
    Matte(c);
    TexFloor(c.tex->wood, x0, x1, zF, zBk, floorY, 16.0f);
    TexFloor(c.tex->wood, dx - dHalf, dx + dHalf, zBk, hallEnd, floorY, 16.0f, Color{170, 160, 150, 255});
    TexQuad(c.tex->rug, {-16, floorY + 0.05f, -28}, {16, floorY + 0.05f, -28}, {16, floorY + 0.05f, -66}, {-16, floorY + 0.05f, -66}, 1, 1);
    Lit(c, true);
    DrawQuad3D({x0, ceilY, zF}, {x0, ceilY, zBk}, {x1, ceilY, zBk}, {x1, ceilY, zF}, Color{238, 234, 226, 255});
    DrawQuad3D({x0, floorY, zF}, {x0, floorY, zBk}, {x0, ceilY, zBk}, {x0, ceilY, zF}, wall);
    DrawQuad3D({x1, floorY, zF}, {x1, ceilY, zF}, {x1, ceilY, zBk}, {x1, floorY, zBk}, wall);
    DrawQuad3D({x0, floorY, zBk}, {dx - dHalf, floorY, zBk}, {dx - dHalf, ceilY, zBk}, {x0, ceilY, zBk}, backWall);
    DrawQuad3D({dx + dHalf, floorY, zBk}, {x1, floorY, zBk}, {x1, ceilY, zBk}, {dx + dHalf, ceilY, zBk}, backWall);
    DrawQuad3D({dx - dHalf, dTop, zBk}, {dx + dHalf, dTop, zBk}, {dx + dHalf, ceilY, zBk}, {dx - dHalf, ceilY, zBk}, backWall);
    const Color hall{150, 132, 112, 255};
    DrawQuad3D({dx - dHalf, floorY, zBk}, {dx - dHalf, floorY, hallEnd}, {dx - dHalf, dTop, hallEnd}, {dx - dHalf, dTop, zBk}, hall);
    DrawQuad3D({dx + dHalf, floorY, zBk}, {dx + dHalf, dTop, zBk}, {dx + dHalf, dTop, hallEnd}, {dx + dHalf, floorY, hallEnd}, hall);
    DrawQuad3D({dx - dHalf, dTop, zBk}, {dx - dHalf, dTop, hallEnd}, {dx + dHalf, dTop, hallEnd}, {dx + dHalf, dTop, zBk}, hall);
    DrawQuad3D({dx - dHalf, floorY, hallEnd}, {dx + dHalf, floorY, hallEnd}, {dx + dHalf, dTop, hallEnd}, {dx - dHalf, dTop, hallEnd},
               Color{120, 104, 88, 255});
    Box(dx - 5.0f, dx + 5.0f, floorY + 12.0f, floorY + 24.0f, hallEnd + 0.3f, hallEnd, Color{60, 90, 130, 255});   // picture at the end
    // Baseboards and ceiling beams.
    Box(x0, x0 + 0.5f, floorY, floorY + 1.8f, zF, zBk, trim);
    Box(x1 - 0.5f, x1, floorY, floorY + 1.8f, zF, zBk, trim);
    Box(x0, dx - dHalf, floorY, floorY + 1.8f, zBk + 0.5f, zBk, trim);
    Box(dx + dHalf, x1, floorY, floorY + 1.8f, zBk + 0.5f, zBk, trim);
    for (float x = x0 + 14.0f; x < x1 - 4.0f; x += 18.0f) Box(x - 0.8f, x + 0.8f, ceilY - 1.8f, ceilY, zF, zBk, Color{120, 88, 62, 255});
    // Table and chairs.
    const float tx = -2.0f, tz = -46.0f, ty = floorY + 15.0f;
    Box(tx - 13, tx + 13, ty - 0.9f, ty, tz + 7, tz - 7, Color{150, 98, 58, 255});
    for (float sx : {-1.0f, 1.0f}) for (float sz : {-1.0f, 1.0f})
        Box(tx + sx * 11.5f - 0.6f, tx + sx * 11.5f + 0.6f, floorY, ty - 0.9f, tz + sz * 5.5f + 0.6f, tz + sz * 5.5f - 0.6f, Color{120, 78, 46, 255});
    auto chair = [&](float x, float z, float facing) {
        const float sy = floorY + 9.0f;
        Box(x - 3.5f, x + 3.5f, sy - 0.8f, sy, z + 3.5f, z - 3.5f, Color{170, 60, 50, 255});
        const float bz = z + facing * 3.2f;
        Box(x - 3.5f, x + 3.5f, sy, sy + 9.0f, bz + 0.4f, bz - 0.4f, Color{160, 55, 46, 255});
        for (float sx : {-1.0f, 1.0f}) for (float sz : {-1.0f, 1.0f})
            Box(x + sx * 3.0f - 0.35f, x + sx * 3.0f + 0.35f, floorY, sy - 0.8f, z + sz * 3.0f + 0.35f, z + sz * 3.0f - 0.35f, Color{90, 60, 40, 255});
    };
    chair(tx - 6.0f, tz + 11.5f, 1.0f);
    chair(tx + 7.0f, tz - 11.5f, -1.0f);
    // Teapot-ish objects on the table.
    DrawSphereEx({tx - 3, ty + 1.6f, tz + 1}, 1.6f, 12, 14, Color{60, 110, 170, 255});
    DrawCylinder({tx + 4, ty, tz - 2}, 0.9f, 0.9f, 2.6f, 14, Color{240, 240, 235, 255});
    // Bookshelf on the back wall.
    const float bx = 24.0f, bw = 26.0f, bh = 34.0f, bd = 5.0f;
    const Color shelfWood{110, 72, 44, 255};
    Box(bx - bw / 2, bx + bw / 2, floorY, floorY + bh, zBk + 0.5f, zBk + 0.01f, shelfWood);              // back panel
    Box(bx - bw / 2, bx - bw / 2 + 0.8f, floorY, floorY + bh, zBk + bd, zBk + 0.01f, shelfWood);         // sides
    Box(bx + bw / 2 - 0.8f, bx + bw / 2, floorY, floorY + bh, zBk + bd, zBk + 0.01f, shelfWood);
    Box(bx - bw / 2, bx + bw / 2, floorY + bh - 0.8f, floorY + bh, zBk + bd, zBk + 0.01f, shelfWood);    // top
    static std::vector<std::pair<Vector3, Vector3>> books;   // (min corner, size) for each book, generated once
    static std::vector<Color> bookColors;
    if (books.empty()) {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(0, 1);
        const Color pal[] = {Color{170, 40, 40, 255}, Color{40, 80, 150, 255}, Color{40, 120, 70, 255}, Color{200, 160, 60, 255},
                             Color{90, 50, 120, 255}, Color{230, 225, 210, 255}, Color{30, 30, 35, 255}};
        for (int shelf = 0; shelf < 4; ++shelf) {
            float x = bx - bw / 2 + 1.0f;
            const float y = floorY + 1.0f + shelf * 8.2f;
            while (x < bx + bw / 2 - 2.0f) {
                const float w = 0.8f + 0.9f * u(rng), h = 4.5f + 2.5f * u(rng);
                if (u(rng) > 0.12f) { books.push_back({{x, y, zBk + 0.6f}, {w * 0.92f, h, 3.6f + 0.6f * u(rng)}});
                                      bookColors.push_back(pal[int(u(rng) * 7) % 7]); }
                x += w;
            }
        }
    }
    for (int shelf = 0; shelf <= 4; ++shelf) {
        const float y = floorY + 1.0f + shelf * 8.2f;
        Box(bx - bw / 2 + 0.6f, bx + bw / 2 - 0.6f, y - 0.5f, y, zBk + bd, zBk + 0.3f, Color{130, 88, 54, 255});
    }
    for (size_t i = 0; i < books.size(); ++i) {
        const auto& b = books[i];
        Box(b.first.x, b.first.x + b.second.x, b.first.y, b.first.y + b.second.y, b.first.z, b.first.z + b.second.z, bookColors[i]);
    }
    // Door trim around the hallway opening.
    Box(dx - 9.5f, dx - 8.0f, floorY, floorY + 31.5f, zBk + 0.8f, zBk, trim);
    Box(dx + 8.0f, dx + 9.5f, floorY, floorY + 31.5f, zBk + 0.8f, zBk, trim);
    Box(dx - 9.5f, dx + 9.5f, floorY + 30.0f, floorY + 31.5f, zBk + 0.8f, zBk, trim);
    // Painting on the left wall.
    const float pz = -60.0f, py = floorY + 26.0f;
    Box(x0, x0 + 0.6f, py - 7.5f, py + 7.5f, pz + 10.5f, pz - 10.5f, Color{150, 110, 40, 255});
    DrawQuad3D({x0 + 0.62f, py, pz + 9.5f}, {x0 + 0.62f, py, pz - 9.5f}, {x0 + 0.62f, py + 6.5f, pz - 9.5f}, {x0 + 0.62f, py + 6.5f, pz + 9.5f}, Color{120, 170, 220, 255});
    DrawQuad3D({x0 + 0.62f, py - 6.5f, pz + 9.5f}, {x0 + 0.62f, py - 6.5f, pz - 9.5f}, {x0 + 0.62f, py, pz - 9.5f}, {x0 + 0.62f, py, pz + 9.5f}, Color{80, 140, 70, 255});
    // Plant near the front right and a floor lamp near the front left: partly hidden by the opening's edges.
    const float px = c.R + 3.0f, pzz = zF - 9.0f;
    DrawCylinder({px, floorY, pzz}, 2.0f, 2.6f, 5.5f, 16, Color{190, 100, 70, 255});
    for (int k = 0; k < 7; ++k) {
        const float a = k * 0.9f;
        DrawSphereEx({px + 2.2f * std::cos(a), floorY + 9.0f + 2.5f * float(k % 3), pzz + 2.2f * std::sin(a)}, 2.3f, 8, 10, Color{50, 130, 60, 255});
    }
    const float fx = c.L - 4.0f, fz = zF - 14.0f;
    DrawCylinder({fx, floorY, fz}, 1.6f, 1.6f, 0.6f, 14, Color{40, 40, 45, 255});
    DrawCylinder({fx, floorY, fz}, 0.25f, 0.25f, 30.0f, 8, Color{40, 40, 45, 255});
    DrawCylinder({fx, floorY + 28.0f, fz}, 3.2f, 1.8f, 4.0f, 18, Color{235, 220, 190, 255});

    Lit(c, false);
    // Pendant lamp near the front: cord + glowing bulb under a shade.
    const Vector3 lamp{1.5f, c.T + 2.0f, zF - 16.0f};
    DrawLine3D({lamp.x, ceilY, lamp.z}, {lamp.x, lamp.y + 2.5f, lamp.z}, Color{30, 30, 30, 255});
    DrawSphereEx({lamp.x, lamp.y, lamp.z}, 1.0f, 10, 12, Color{255, 236, 170, 255});
    Lit(c, true);
    DrawCylinder({lamp.x, lamp.y - 0.4f, lamp.z}, 0.6f, 3.4f, 3.0f, 20, Color{40, 70, 60, 255});
    Lit(c, false);
    // A window on the right wall showing bright sky.
    DrawQuad3D({x1 - 0.05f, floorY + 14.0f, -28.0f}, {x1 - 0.05f, floorY + 32.0f, -28.0f}, {x1 - 0.05f, floorY + 32.0f, -48.0f},
               {x1 - 0.05f, floorY + 14.0f, -48.0f}, Color{190, 220, 250, 255});
    BeginShadows();
    DropShadow({tx, floorY + 14.0f, tz}, 12.0f, floorY, L, 0.06f, 1.4f);
    DropShadow({tx - 6.0f, floorY + 8.0f, tz + 11.5f}, 4.0f, floorY, L, 0.07f);
    DropShadow({tx + 7.0f, floorY + 8.0f, tz - 11.5f}, 4.0f, floorY, L, 0.07f);
    DropShadow({px, floorY + 6.0f, pzz}, 3.5f, floorY, L, 0.06f);
    EndShadows();
}

// 4. SPACE: a porthole onto an asteroid field. Asteroids right behind the glass, a station and moon in the middle
//    distance, a ringed planet and stars far away. Everything slowly rotates.
void SceneSpace(const SceneCtx& c) {
    struct Rock { Vector3 p; float r; Vector3 axis; float speed, phase; Color col; Vector3 squash; };
    struct Star { Vector3 p; float s; Color col; };
    static std::vector<Rock> rocks;
    static std::vector<Star> stars;
    if (rocks.empty()) {
        std::mt19937 rng(21);
        std::uniform_real_distribution<float> u(0, 1);
        // A sparse field: a few small rocks just behind the glass, more further out, with clear lines of sight.
        for (int i = 0; i < 30; ++i) {
            const float z = -6.0f - 260.0f * u(rng) * u(rng);
            const float spread = 8.0f + std::abs(z) * 0.9f;
            Rock r;
            r.p = {(u(rng) * 2 - 1) * spread, 1.0f + (u(rng) * 2 - 1) * spread * 0.6f, c.zG + z};
            r.r = (0.35f + 1.2f * u(rng)) * (1.0f + std::abs(z) / 60.0f);
            r.axis = Vector3Normalize({u(rng) - 0.5f, u(rng) - 0.5f, u(rng) - 0.5f});
            r.speed = 8.0f + 30.0f * u(rng); r.phase = 360.0f * u(rng);
            const unsigned char g = (unsigned char)(95 + 60 * u(rng));
            r.col = Color{g, (unsigned char)(g * 0.9f), (unsigned char)(g * 0.8f), 255};
            r.squash = {0.8f + 0.4f * u(rng), 0.6f + 0.3f * u(rng), 0.8f + 0.4f * u(rng)};
            rocks.push_back(r);
        }
        for (int i = 0; i < 2200; ++i) {
            const float z = -300.0f - 3500.0f * u(rng);
            Star s;
            s.p = {(u(rng) * 2 - 1) * std::abs(z) * 1.1f, (u(rng) * 2 - 1) * std::abs(z) * 0.7f, z};
            s.s = std::abs(z) * (0.0012f + 0.0018f * u(rng));
            const unsigned char b = (unsigned char)(170 + 85 * u(rng));
            s.col = u(rng) > 0.85f ? Color{b, (unsigned char)(b * 0.85f), (unsigned char)(b * 0.7f), 255} : Color{b, b, 255, 255};
            stars.push_back(s);
        }
    }
    const float t = float(c.time);

    Lit(c, false);
    for (const auto& s : stars) DrawCube(s.p, s.s, s.s, s.s, s.col);
    const Vector3 planet{-420.0f, 140.0f, -1900.0f};

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawReveal(c, 1.6f, Color{70, 74, 84, 255}, Color{62, 66, 76, 255}, Color{80, 84, 94, 255});
    DrawSphereEx(planet, 250.0f, 40, 48, Color{215, 150, 90, 255});
    DrawSphereEx({260.0f, -90.0f, -620.0f}, 48.0f, 24, 28, Color{170, 170, 175, 255});
    // Planet ring: a tilted flat annulus (after the planet, so the planet hides its far half).
    Lit(c, false);
    {
        const float r0 = 330.0f, r1 = 470.0f;
        const float tilt = 20.0f * DEG2RAD, roll = -15.0f * DEG2RAD;   // ring plane tipped toward us, then rolled
        auto ringPt = [&](float r, float a) {
            const float x = r * std::cos(a), z0 = r * std::sin(a);
            const float y1 = -z0 * std::sin(tilt), z1 = z0 * std::cos(tilt);
            return Vector3{planet.x + x * std::cos(roll) - y1 * std::sin(roll),
                           planet.y + x * std::sin(roll) + y1 * std::cos(roll), planet.z + z1};
        };
        constexpr int kSeg = 96;
        for (int i = 0; i < kSeg; ++i) {
            const float a0 = 2.0f * PI * i / kSeg, a1 = 2.0f * PI * (i + 1) / kSeg;
            const Color col = Fade(i % 2 ? Color{215, 195, 150, 255} : Color{200, 180, 140, 255}, 0.55f);
            DrawQuad3D(ringPt(r0, a0), ringPt(r1, a0), ringPt(r1, a1), ringPt(r0, a1), col);
        }
    }
    Lit(c, true);
    // Space station: a rotating hub with arms and solar panels, in the middle distance.
    rlPushMatrix();
    rlTranslatef(-30.0f, 14.0f, c.zG - 120.0f);
    rlRotatef(t * 6.0f, 0, 0, 1);
    DrawCylinderEx({-6, 0, 0}, {6, 0, 0}, 1.4f, 1.4f, 16, Color{210, 212, 220, 255});
    DrawSphereEx({0, 0, 0}, 2.4f, 14, 16, Color{230, 230, 236, 255});
    for (float sx : {-1.0f, 1.0f}) {
        Box(sx * 7.0f - 0.2f, sx * 7.0f + 0.2f, -9, 9, 0.2f, -0.2f, Color{200, 200, 210, 255});
        Box(sx * 7.0f - 2.6f, sx * 7.0f + 2.6f, 2.5f, 8.5f, 0.1f, -0.1f, Color{40, 70, 150, 255});
        Box(sx * 7.0f - 2.6f, sx * 7.0f + 2.6f, -8.5f, -2.5f, 0.1f, -0.1f, Color{40, 70, 150, 255});
    }
    rlPopMatrix();
    for (const auto& r : rocks) {
        rlPushMatrix();
        rlTranslatef(r.p.x, r.p.y, r.p.z);
        rlRotatef(r.phase + t * r.speed, r.axis.x, r.axis.y, r.axis.z);
        rlScalef(r.squash.x, r.squash.y, r.squash.z);
        DrawSphereEx({0, 0, 0}, r.r, 4, 6, r.col);   // low-poly sphere = faceted rock
        rlPopMatrix();
    }
    Lit(c, false);
    DrawGlassEdge(c, Color{120, 125, 135, 255});
}

// 5. SHAPES: the original floating shapes in a gridded room, with shadows and hanging strings.
void SceneShapes(const SceneCtx& c) {
    struct Shape { int type; Vector3 p; float size; Color col; };
    static const Shape shapes[] = {
        {0, {-4.0f, 2.5f, -3.0f}, 1.4f, RED},      {1, {-1.5f, 3.6f, -8.0f}, 1.6f, BLUE},
        {2, {3.0f, 1.5f, -14.0f}, 2.0f, GREEN},    {3, {4.0f, -1.5f, -5.0f}, 1.6f, ORANGE},
        {4, {-3.5f, -1.0f, -11.0f}, 1.8f, PURPLE}, {1, {0.0f, 0.6f, 3.2f}, 0.9f, GOLD},
        {0, {1.0f, 3.0f, -20.0f}, 2.2f, SKYBLUE},  {2, {-4.4f, 0.8f, -18.0f}, 2.0f, PINK},
        {3, {2.2f, -2.8f, -9.0f}, 1.6f, LIME},     {4, {1.6f, -0.6f, 1.9f}, 0.9f, VIOLET},
        {0, {-1.8f, -2.9f, -2.0f}, 1.2f, MAROON},  {1, {-0.5f, -2.2f, -16.0f}, 1.8f, DARKBLUE},
        {2, {4.2f, 3.6f, -10.0f}, 1.4f, DARKGREEN},{3, {-2.6f, 1.4f, -0.5f}, 1.0f, BROWN},
        {4, {3.5f, -2.4f, -22.0f}, 2.0f, DARKPURPLE}};
    const float zB = c.zG - 25.0f;
    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawBoxRoom(c.L, c.R, c.B, c.T, c.zG, zB, Color{196, 198, 206, 255}, Color{222, 224, 232, 255}, Color{236, 237, 242, 255},
                Color{210, 212, 220, 255});
    for (const auto& s : shapes) {
        const Vector3 p{s.p.x, s.p.y, s.p.z + c.zG - 1.0f};
        switch (s.type) {
            case 0: DrawCube(p, s.size, s.size, s.size, s.col); break;
            case 1: DrawSphereEx(p, s.size * 0.5f, 24, 24, s.col); break;
            case 2: DrawCylinder({p.x, p.y - s.size * 0.5f, p.z}, s.size * 0.35f, s.size * 0.35f, s.size, 24, s.col); break;
            case 3: DrawCylinder({p.x, p.y - s.size * 0.5f, p.z}, 0.0f, s.size * 0.45f, s.size, 24, s.col); break;
            default: DrawCapsule({p.x, p.y - s.size * 0.3f, p.z}, {p.x, p.y + s.size * 0.3f, p.z}, s.size * 0.28f, 16, 12, s.col);
        }
    }
    Lit(c, false);
    DrawGrid(c.L, c.R, c.B, c.T, c.zG, zB, 2.5f, 1.25f, Color{118, 122, 136, 255});
    DrawGlassEdge(c, Color{25, 25, 30, 255});
    for (const auto& s : shapes) {
        const Vector3 p{s.p.x, s.p.y, s.p.z + c.zG - 1.0f};
        if (p.z > c.zG - 0.2f) continue;
        DrawLine3D({p.x, p.y + s.size * 0.5f, p.z}, {p.x, c.T, p.z}, Color{80, 82, 95, 255});
    }
    BeginShadows();
    for (const auto& s : shapes) {
        const Vector3 p{s.p.x, s.p.y, s.p.z + c.zG - 1.0f};
        if (p.z > c.zG - 0.5f) continue;
        DropShadow(p, s.size * 0.55f, c.B, c.lightDir, 0.02f);
    }
    EndShadows();
}

// ---------------------------------------------------------------- list ------
const SceneDef kScenes[] = {
    {"Window onto a garden", Color{150, 198, 240, 255}, {0.80f, 0.87f, 0.93f}, 60.0f, 3200.0f, 0.92f, {-0.45f, -1.0f, -0.55f}, SceneWindow,
     "A real window with a deep frame, looking out onto a garden. The flower box right outside moves against the "
     "trees and hills far away: the biggest depth difference of all the scenes."},
    {"Targets", Color{8, 8, 12, 255}, {0.0f, 0.0f, 0.0f}, 4.0f, 70.0f, 0.65f, {0.3f, -1.0f, -0.5f}, SceneTargets,
     "A tribute to Johnny Lee's Wii head-tracking demo: a deep gridded box with targets on sticks. A few targets "
     "float in front of the screen."},
    {"Furnished room", Color{30, 24, 20, 255}, {0.24f, 0.18f, 0.14f}, 25.0f, 320.0f, 0.35f, {-0.35f, -1.0f, -0.45f}, SceneRoom,
     "A furnished room seen through a doorway. Familiar objects (table, chairs, shelves, lamp) help your eyes "
     "judge distance; lean to see around the edges of the opening."},
    {"Space porthole", Color{2, 2, 8, 255}, {0.0f, 0.0f, 0.0f}, 1e5f, 2e5f, 0.0f, {-1.0f, -0.25f, -0.35f}, SceneSpace,
     "A porthole onto an asteroid field: asteroids just behind the glass, a station and moon in the middle "
     "distance, a ringed planet and stars far away. Everything slowly rotates."},
    {"Floating shapes", Color{30, 30, 36, 255}, {0.55f, 0.57f, 0.63f}, 0.0f, 32.0f, 0.55f, {0.35f, -1.0f, -0.45f}, SceneShapes,
     "Shapes hanging on strings in a gridded room, with soft shadows on the floor."},
};
constexpr int kSceneCount = int(sizeof(kScenes) / sizeof(kScenes[0]));

// The pen-tip test uses the lighting/fog of the shapes scene.
const SceneDef& kPenTipSceneDef = kScenes[4];

// Imported models: a neutral studio, no fog (RenderScene adjusts fog and light for files with a camera).
const SceneDef kImportedSceneDef = {"", Color{24, 26, 32, 255}, {0.09f, 0.10f, 0.13f}, 1e5f, 2e5f, 0.0f,
                                    {-0.35f, -1.0f, -0.45f}, nullptr, ""};

// Light for files with a camera, in the file's world (Y up): from above, like Blender's sun from the top.
constexpr Vector3 kFileWorldLight{-0.35f, -1.0f, -0.45f};

Vector3 RotateDir(const Matrix& m, Vector3 d) {
    return Vector3Normalize(Vector3Subtract(Vector3Transform(d, m), Vector3Transform({0, 0, 0}, m)));
}

// Draws a convex polygon cut to z <= zMax, so backdrop geometry never crosses the glass toward the viewer.
void DrawClippedPolygon(const Vector3* p, int n, float zMax, Color col) {
    Vector3 out[16];
    int k = 0;
    for (int i = 0; i < n && k < 15; ++i) {
        const Vector3 a = p[i], b = p[(i + 1) % n];
        const bool inA = a.z <= zMax, inB = b.z <= zMax;
        if (inA) out[k++] = a;
        if (inA != inB) out[k++] = Vector3Lerp(a, b, (zMax - a.z) / (b.z - a.z));
    }
    for (int i = 1; i + 1 < k; ++i) DrawTriangle3D(out[0], out[i], out[i + 1], col);
}

// The floor under a file with a camera, in the file's own world (metres, Y up).
struct FileFloor {
    float y = 0, cx = 0, cz = 0, radius = 20;   // centered under the model
    float farFromCamera = 40;                   // camera to the floor's furthest corner
};

FileFloor FloorFor(const LoadedModel& lm) {
    const BoundingBox& b = lm.bounds;
    FileFloor f;
    f.cx = (b.min.x + b.max.x) * 0.5f;
    f.cz = (b.min.z + b.max.z) * 0.5f;
    f.y = b.min.y - 0.01f;   // 1 cm under the lowest point (avoids flicker against a ground plane in the file)
    const float camDist = std::hypot(lm.camera.m12 - f.cx, lm.camera.m14 - f.cz);
    f.radius = std::max({20.0f, 3.0f * std::max(b.max.x - b.min.x, b.max.z - b.min.z), 2.5f * camDist});
    f.farFromCamera = camDist + f.radius * 1.42f;
    return f;
}


// ---------------------------------------------------------------- imported models ------
// Model space -> world. With a camera in the file: that camera becomes the eye at the sitting position, at real
// scale (file units are metres). Without one: scaled to fit the opening, resting on its bottom edge, just
// behind the glass.
// Half the diagonal of the model's footprint (x/z): how far it reaches from its vertical axis at any spin angle.
float FootprintRadius(const BoundingBox& b) {
    return 0.5f * std::hypot(b.max.x - b.min.x, b.max.z - b.min.z);
}

Matrix ImportedModelTransform(const SceneCtx& c, const LoadedModel& lm, float neutralDistMm) {
    if (lm.hasCamera) {
        const float s = 1000.0f * c.unitsPerMm;
        const Vector3 eye{0, kScreenCenterY, c.zG + neutralDistMm * c.unitsPerMm};
        return MatrixMultiply(MatrixMultiply(MatrixInvert(lm.camera), MatrixScale(s, s, s)),
                              MatrixTranslate(eye.x, eye.y, eye.z));
    }
    // Fitted so it still fits at any spin angle: width and depth use the radius of its footprint.
    const float W = c.R - c.L, H = c.T - c.B;
    const BoundingBox& b = lm.bounds;
    const float sizeY = std::max(b.max.y - b.min.y, 1e-4f);
    const float radius = std::max(FootprintRadius(b), 1e-4f);
    const float k = std::min({0.70f * H / sizeY, 0.80f * W / (2 * radius), 1.5f * H / (2 * radius)});
    const Vector3 ctr = lm.center();
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(-ctr.x, -b.min.y, -ctr.z), MatrixScale(k, k, k)),
                          MatrixTranslate(0, c.B, c.zG - 0.12f * H - k * radius));
}

// The box swept by a model spinning about its vertical center axis.
BoundingBox SpinBounds(const BoundingBox& b) {
    const float r = FootprintRadius(b);
    const float cx = (b.min.x + b.max.x) * 0.5f, cz = (b.min.z + b.max.z) * 0.5f;
    return {{cx - r, b.min.y, cz - r}, {cx + r, b.max.y, cz + r}};
}

BoundingBox TransformBox(const BoundingBox& b, const Matrix& m) {
    BoundingBox out{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (int i = 0; i < 8; ++i) {
        const Vector3 p = Vector3Transform({(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                                           (i & 4) ? b.max.z : b.min.z}, m);
        out.min = Vector3Min(out.min, p);
        out.max = Vector3Max(out.max, p);
    }
    return out;
}

void SceneImported(const SceneCtx& c, int libraryIndex, const RenderInputs& in) {
    const float H = c.T - c.B;
    LoadedModel* lm = GetLoadedModel(libraryIndex);
    Matrix m = MatrixIdentity();
    BoundingBox wb{{c.L, c.B, c.zG - H}, {c.R, c.B, c.zG - H}};   // empty studio if the model failed to load
    if (lm) {
        if (lm->clipCount() > 0 && in.playback) lm->pose(in.playback->clip, in.playback->frame);
        m = ImportedModelTransform(c, *lm, in.rig.neutralDistMm);
        wb = TransformBox(SpinBounds(lm->bounds), m);   // room sized for any spin angle, so the walls stay put
    }
    const Matrix spin = lm && in.playback ? lm->spinTransform(in.playback->yaw, in.playback->pitch) : MatrixIdentity();

    const bool backdrop = ImportedModelInfo(libraryIndex).backdrop;
    if (backdrop && lm && lm->hasCamera) {
        // A checkered floor in the file's world, under the model, so it lines up with the scene as staged in
        // Blender (a box room around the screen would be tilted against it whenever the camera looks down).
        const FileFloor f = FloorFor(*lm);
        const BoundingBox& b = lm->bounds;
        const float zMax = c.zG - 0.01f;
        constexpr int N = 48;
        const float tile = 2.0f * f.radius / N;
        Matte(c);
        rlDrawRenderBatchActive();
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                const float x0 = f.cx - f.radius + i * tile, z0 = f.cz - f.radius + j * tile;
                const Vector3 q[4] = {Vector3Transform({x0, f.y, z0}, m), Vector3Transform({x0 + tile, f.y, z0}, m),
                                      Vector3Transform({x0 + tile, f.y, z0 + tile}, m),
                                      Vector3Transform({x0, f.y, z0 + tile}, m)};
                DrawClippedPolygon(q, 4, zMax, ((i + j) & 1) ? Color{150, 154, 166, 255} : Color{132, 136, 150, 255});
            }
        }
        // Soft contact shadow straight under the model.
        Lit(c, false);
        BeginShadows();
        const float r = 0.45f * std::max(b.max.x - b.min.x, b.max.z - b.min.z);
        const float cx = (b.min.x + b.max.x) * 0.5f, cz = (b.min.z + b.max.z) * 0.5f, y = f.y + 0.005f;
        for (const float scale : {1.0f, 0.7f, 0.45f}) {
            constexpr int S = 32;
            for (int s = 0; s < S; ++s) {
                const float a0 = 2 * float(kPi) * s / S, a1 = 2 * float(kPi) * (s + 1) / S;
                const Vector3 tri[3] = {
                    Vector3Transform({cx, y, cz}, m),
                    Vector3Transform({cx + r * scale * std::cos(a1), y, cz + r * scale * std::sin(a1)}, m),
                    Vector3Transform({cx + r * scale * std::cos(a0), y, cz + r * scale * std::sin(a0)}, m)};
                DrawClippedPolygon(tri, 3, zMax, Fade(BLACK, 0.09f));
            }
        }
        EndShadows();
    } else if (backdrop) {
        // A box room that contains the opening and the whole model: its walls and grid give the depth cues.
        const float pad = 0.1f * H;
        const float L = std::min(c.L, wb.min.x - pad), R = std::max(c.R, wb.max.x + pad);
        const float B = std::min(c.B, wb.min.y), T = std::max(c.T, wb.max.y + pad);
        const float back = std::min(c.zG - 2.0f * H, wb.min.z - pad);
        Lit(c, true);
        rlDrawRenderBatchActive();
        DrawBoxRoom(L, R, B, T, c.zG, back, Color{150, 154, 166, 255}, Color{176, 180, 192, 255},
                    Color{196, 199, 208, 255}, Color{164, 168, 180, 255});
        Lit(c, false);
        const float ring = std::max(2.5f, (c.zG - back) / 40.0f);
        const float line = std::max(1.25f, std::max(R - L, T - B) / 40.0f);
        DrawGrid(L, R, B, T, c.zG, back, ring, line, Color{110, 114, 128, 255});
        if (lm) {
            BeginShadows();
            const Vector3 center{(wb.min.x + wb.max.x) * 0.5f, wb.min.y, (wb.min.z + wb.max.z) * 0.5f};
            DropShadow(center, 0.35f * std::max(wb.max.x - wb.min.x, wb.max.z - wb.min.z), B, c.lightDir, 0.02f);
            EndShadows();
        }
    }
    if (!lm) return;

    c.sh->setMaterial(true, 0.6f);
    if (c.sh->ok)
        for (int i = 0; i < lm->model.materialCount; ++i) lm->model.materials[i].shader = c.sh->sh;
    // Mesh by mesh: object keyframes give each mesh its own transform, then the drag spin, then the placement.
    const Matrix placed = MatrixMultiply(spin, m);
    for (int i = 0; i < lm->model.meshCount; ++i)
        DrawMesh(lm->model.meshes[i], lm->model.materials[lm->model.meshMaterial[i]],
                 MatrixMultiply(lm->meshTransform(i), placed));
    c.sh->resetModelMatrix();
}

// ---------------------------------------------------------------- frame -----
void RenderWithScene(const SceneDef& sd, const RenderInputs& in, const std::function<void(const SceneCtx&)>& draw) {
    const ScreenGeometry& g = in.geo;
    const SceneShader& scene = in.shader;
    const Vector3 eyeWorld = in.rig.eyeWorld;
    const float screenDist = in.rig.screenDist();
    const Vector3 light = Vector3Normalize(sd.light);
    if (scene.ok) {
        const float fogNear = screenDist + sd.fogStart, fogFar = screenDist + sd.fogEnd;
        SetShaderValue(scene.sh, scene.viewPos, &eyeWorld, SHADER_UNIFORM_VEC3);
        SetShaderValue(scene.sh, scene.lightDir, &light, SHADER_UNIFORM_VEC3);
        SetShaderValue(scene.sh, scene.fogColor, &sd.fog, SHADER_UNIFORM_VEC3);
        SetShaderValue(scene.sh, scene.fogNear, &fogNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(scene.sh, scene.fogFar, &fogFar, SHADER_UNIFORM_FLOAT);
        SetShaderValue(scene.sh, scene.fogMax, &sd.fogMax, SHADER_UNIFORM_FLOAT);
    }
    Camera3D camera{};
    camera.position = eyeWorld;
    camera.target = {eyeWorld.x, eyeWorld.y, eyeWorld.z - 1.0f};   // always facing straight into the screen
    camera.up = {0, 1, 0};
    camera.fovy = 45.0f;                                            // unused: projection replaced below
    camera.projection = CAMERA_PERSPECTIVE;
    ClearBackground(sd.sky);
    BeginMode3D(camera);
    ApplyOffAxisProjection(eyeWorld, g.screenWidth, g.screenHeight, screenDist);
    if (scene.ok) BeginShaderMode(scene.sh);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();   // the shader orients normals toward the viewer, so both faces shade correctly

    const SceneCtx ctx{g.L, g.R, g.B, g.T, kScreenPlaneZ, GetTime(), &scene, &in.textures, light, g.unitsPerMm};
    draw(ctx);

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    if (scene.ok) EndShaderMode();
    EndMode3D();
}

} // namespace

int SceneCount() { return kSceneCount + ImportedModelCount(); }
int BuiltInSceneCount() { return kSceneCount; }
bool IsImportedScene(int index) { return index >= kSceneCount && index < SceneCount(); }
int ImportedIndex(int sceneIndex) { return IsImportedScene(sceneIndex) ? sceneIndex - kSceneCount : -1; }

std::string SceneName(int index) {
    if (index >= 0 && index < kSceneCount) return kScenes[index].name;
    return ImportedModelName(ImportedIndex(index));
}

std::string SceneDescription(int index) {
    if (index >= 0 && index < kSceneCount) return kScenes[index].description;
    return ImportedModelInfo(ImportedIndex(index)).description;
}

void RenderScene(int sceneIndex, const RenderInputs& in) {
    const int index = std::clamp(sceneIndex, 0, SceneCount() - 1);
    if (IsImportedScene(index)) {
        SceneDef sd = kImportedSceneDef;
        const LoadedModel* lm = GetLoadedModel(ImportedIndex(index));
        if (lm && lm->hasCamera) {
            // Light from above in the file's world, not relative to the screen.
            const Matrix view = MatrixInvert(lm->camera);
            sd.light = RotateDir(view, kFileWorldLight);
            if (ImportedModelInfo(ImportedIndex(index)).backdrop) {
                // Fog that fades the floor's far edge into the background but starts behind the whole model.
                const BoundingBox& b = lm->bounds;
                float modelFar = 0;
                for (int i = 0; i < 8; ++i)
                    modelFar = std::max(modelFar, Vector3Length(Vector3Transform(
                        {(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z}, view)));
                const float u = 1000.0f * in.geo.unitsPerMm;   // world units per metre
                const FileFloor f = FloorFor(*lm);
                sd.fogEnd = std::min(0.9f * f.farFromCamera * u, 0.9f * kFarPlane);
                sd.fogStart = std::min(std::max(1.05f * modelFar, 0.4f * f.farFromCamera) * u, 0.95f * sd.fogEnd);
                sd.fogMax = 1.0f;
            }
        }
        RenderWithScene(sd, in, [&](const SceneCtx& c) { SceneImported(c, ImportedIndex(index), in); });
        return;
    }
    const SceneDef& sd = kScenes[index];
    RenderWithScene(sd, in, [&](const SceneCtx& c) { sd.draw(c); });
}

void RenderPenTipTest(double penTipMm, const RenderInputs& in) {
    RenderWithScene(kPenTipSceneDef, in, [&](const SceneCtx& c) {
        Lit(c, true);
        rlDrawRenderBatchActive();
        DrawBoxRoom(c.L, c.R, c.B, c.T, c.zG, c.zG - 25.0f, Color{196, 198, 206, 255}, Color{222, 224, 232, 255},
                    Color{236, 237, 242, 255}, Color{210, 212, 220, 255});
        const Vector3 tip{0, kScreenCenterY, c.zG + float(penTipMm) * c.unitsPerMm};
        DrawSphereEx(tip, 2.5f * c.unitsPerMm, 16, 16, RED);
        Lit(c, false);
        DrawGrid(c.L, c.R, c.B, c.T, c.zG, c.zG - 25.0f, 2.5f, 1.25f, Color{118, 122, 136, 255});
        const float cr = 15.0f * c.unitsPerMm, z = c.zG + 0.005f;
        DrawLine3D({-cr, kScreenCenterY, z}, {cr, kScreenCenterY, z}, BLACK);
        DrawLine3D({0, kScreenCenterY - cr, z}, {0, kScreenCenterY + cr, z}, BLACK);
    });
}

} // namespace tess
