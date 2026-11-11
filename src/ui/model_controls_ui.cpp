#include "ui/model_controls_ui.h"

#include "core/text.h"
#include "render/scenes.h"
#include "ui/ui_widgets.h"

#include <algorithm>
#include <cmath>

namespace tess {
namespace {

constexpr float kDegPerPixel = 0.4f;     // drag sensitivity
constexpr float kMaxPitch = 80.0f;
constexpr float kSpinFriction = 3.0f;    // per second: how quickly a flick slows down
constexpr double kDoubleClickS = 0.3;

// The model of the scene being shown, if it is an imported one and the page is visible.
LoadedModel* ShownModel(const AppState& st) {
    if (st.page != Page::Scene || st.settingsOpen || !IsImportedScene(st.cfg.scene)) return nullptr;
    return GetLoadedModel(ImportedIndex(st.cfg.scene));
}

// Where the animation bar sits (same place in update() and draw()).
Rectangle BarRect(const UiFrame& f) {
    const float sw = float(f.geo.sw), sh = float(f.geo.sh);
    const float w = std::min(sw - 40, 760.0f);
    return {(sw - w) / 2, sh - 124, w, 72};
}

bool BarVisible(const LoadedModel& m, const UiFrame& f) { return m.clipCount() > 0 && f.mouseIdleS <= 2.5; }

// The Settings button of SceneOverlayUi (menu_ui.cpp) while it is showing.
bool OverSettingsButton(Vector2 p, const UiFrame& f) {
    return f.mouseIdleS <= 2.5 && CheckCollisionPointRec(p, {float(f.geo.sw) - 136, 12, 124, 36});
}

} // namespace

bool ModelControlsUi::update(AppState& st, const UiFrame& f) {
    const LoadedModel* m = ShownModel(st);
    if (!m) { dragging_ = false; return false; }
    ModelPlayback& p = st.playback;
    if (m->clipCount() > 0 && IsKeyPressed(KEY_SPACE)) p.playing = !p.playing;

    // Click and drag spins the model; double-click puts it back.
    const float dt = GetFrameTime();
    const Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !OverSettingsButton(mouse, f) &&
        !(BarVisible(*m, f) && CheckCollisionPointRec(mouse, BarRect(f)))) {
        if (GetTime() - lastClick_ < kDoubleClickS) {
            p.yaw = p.pitch = p.yawVelocity = 0;
            dragging_ = false;
            lastClick_ = 0;
            return false;
        }
        lastClick_ = GetTime();
        dragging_ = true;
        p.yawVelocity = 0;
    }
    if (dragging_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        const Vector2 d = GetMouseDelta();
        p.yaw += d.x * kDegPerPixel;
        p.pitch = std::clamp(p.pitch + d.y * kDegPerPixel, -kMaxPitch, kMaxPitch);
        if (dt > 0) p.yawVelocity += (d.x * kDegPerPixel / dt - p.yawVelocity) * 0.5f;   // smoothed, for the flick
    } else {
        dragging_ = false;
        p.yaw += p.yawVelocity * dt;
        p.yawVelocity *= std::exp(-kSpinFriction * dt);
        if (std::fabs(p.yawVelocity) < 2.0f) p.yawVelocity = 0;
    }
    p.yaw = std::fmod(p.yaw, 360.0f);
    return false;   // the scene hotkeys still apply
}

void ModelControlsUi::draw(AppState& st, const UiFrame& f) {
    if (st.page != Page::Scene || st.settingsOpen || !IsImportedScene(st.cfg.scene)) return;
    const float sw = float(f.geo.sw), sh = float(f.geo.sh);
    const LoadedModel* m = ShownModel(st);
    if (!m) {
        const std::string& err = ImportedModelError(ImportedIndex(st.cfg.scene));
        const std::string msg = "This model could not be loaded. " + err;
        const int w = std::min(int(sw) - 40, MeasureText(msg.c_str(), 18) + 40);
        DrawRectangleRounded({(sw - w) / 2, sh / 2 - 40, float(w), 80}, 0.2f, 8, Fade(theme::kPanel, 0.9f));
        DrawCenteredText(msg.c_str(), 18, sh / 2 - 22, theme::kWarn);
        DrawCenteredText("Delete it in Select Scene and import it again.", 16, sh / 2 + 6, theme::kMuted);
        return;
    }
    if (f.mouseIdleS > 2.5) return;
    const char* hint = "Drag to spin the model, double-click to reset";
    if (!BarVisible(*m, f)) {
        DrawCenteredText(hint, 15, sh - 70, Fade(theme::kInk, 0.8f));
        return;
    }

    ModelPlayback& p = st.playback;
    p.clip = std::clamp(p.clip, 0, m->clipCount() - 1);
    const AnimClip& anim = m->clips[p.clip];
    const Rectangle bar = BarRect(f);
    const float x = bar.x, y = bar.y, w = bar.width;
    DrawRectangleRounded(bar, 0.2f, 8, Fade(theme::kPanel, 0.88f));
    DrawCenteredText(hint, 15, y - 22, Fade(theme::kInk, 0.8f));

    const float bx = x + 14, by = y + 18;
    if (Button({bx, by, 92, 36}, p.playing ? "Pause" : "Play", true, 16)) p.playing = !p.playing;
    float sx = bx + 104;
    if (m->clipCount() > 1) {
        if (Button({sx, by, 36, 36}, "<", true, 18)) { p.clip = (p.clip + m->clipCount() - 1) % m->clipCount(); p.frame = 0; }
        if (Button({sx + 42, by, 36, 36}, ">", true, 18)) { p.clip = (p.clip + 1) % m->clipCount(); p.frame = 0; }
        sx += 90;
    }
    const double length = std::max(0, anim.frameCount - 1) / double(kAnimFramesPerSecond);
    double seconds = p.frame / kAnimFramesPerSecond;
    const std::string label = Fmt("%s  (%d/%d)   Space = play/pause", anim.name.c_str(), p.clip + 1, m->clipCount());
    if (Slider(sx, y + 12, x + w - 16 - sx, label.c_str(), seconds, 0.0, std::max(length, 1e-3), "%.2f s"))
        p.frame = seconds * kAnimFramesPerSecond;
}

} // namespace tess
