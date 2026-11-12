#include "ui/hud.h"

#include "core/text.h"
#include "render/scenes.h"

#include <algorithm>

namespace tess {

void Hud::draw(AppState& st, const UiFrame& f) {
    if (st.page != Page::Scene || !st.cfg.showHud) return;
    const Snapshot& snap = f.snap;
    const ScreenGeometry& g = f.geo;
    const int n = SceneCount();

    DrawRectangle(0, 0, std::min(g.sw, 900), 150, Fade(BLACK, 0.5f));
    DrawFPS(10, 8);
    DrawText(Fmt("Scene %d/%d: %s   (1-%d / TAB)", st.cfg.scene + 1, n, SceneName(st.cfg.scene).c_str(), n).c_str(),
             110, 10, 18, RAYWHITE);
    DrawText("[ ] strength | P geometry | R recenter | L prediction | C setup | H hud | F11 | ESC",
             10, 34, 16, RAYWHITE);
    DrawText(Fmt("Strength %.2f | %s | prediction %s (%.0f ms) | latency %.0f ms",
                 st.cfg.strength, st.physicalAlignment ? "true geometry" : "recentered",
                 st.cfg.prediction ? "on" : "off", f.predictS * 1000.0, snap.procMs).c_str(), 10, 56, 16, RAYWHITE);
    DrawText(Fmt("Eye vs window center (mm): X %.0f  Y %.0f  Z %.0f   (Z should match a tape measure)",
                 snap.valid ? snap.pos[0] - g.winOffXMm : 0.0, snap.valid ? snap.pos[1] - g.winOffYMm : 0.0,
                 snap.valid ? snap.pos[2] : 0.0).c_str(), 10, 78, 16, RAYWHITE);
    DrawText(Fmt("Screen %.0f x %.0f mm (%s) | webcam FOV %s | camera %dx%d @ %.0f fps | %.1f ms/frame",
                 f.dims.wMm, f.dims.hMm, f.dims.source.c_str(), st.fovLabel().c_str(), snap.camW, snap.camH,
                 snap.camFps, snap.workMs).c_str(), 10, 100, 16, RAYWHITE);
    DrawText(Fmt("%s | YuNet %.2f | reprojection %.1f px", snap.status.c_str(), snap.faceScore,
                 snap.reprojErr).c_str(), 10, 122, 16, snap.tracking ? GREEN : ORANGE);
}

void DrawToast(const Toast& toast, int sw, int sh) {
    if (!toast.visible()) return;
    const int w = std::min(sw - 20, MeasureText(toast.text.c_str(), 18) + 24);
    DrawRectangle(10, sh - 44, w, 34, Fade(BLACK, 0.8f));
    DrawText(toast.text.c_str(), 22, sh - 36, 18, YELLOW);
}

} // namespace tess
