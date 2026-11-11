#include "core/app_state.h"

#include "core/text.h"
#include "render/scenes.h"

#include <raylib.h>
#include <algorithm>

namespace tess {

void Toast::show(const std::string& msg, double seconds) { text = msg; until = GetTime() + seconds; }
bool Toast::visible() const { return !text.empty() && GetTime() < until; }

namespace {
ModelPlayback FreshPlayback(int scene) {
    ModelPlayback p;
    p.playing = !IsImportedScene(scene) || ImportedModelInfo(ImportedIndex(scene)).autoplay;
    return p;
}
} // namespace

void AppState::openSceneSelect() {
    page = Page::SceneSelect;
    previewScene = -1;
}

void AppState::previewSceneAt(int index) {
    if (index < 0 || index >= SceneCount()) { previewScene = -1; return; }
    if (index != previewScene) playback = FreshPlayback(index);
    previewScene = index;
}

void AppState::enterScene(int index) {
    index = std::clamp(index, 0, SceneCount() - 1);
    // Coming from its preview, the animation carries on where it was; the spin always starts from the staging.
    if (index == previewScene) { playback.yaw = playback.pitch = playback.yawVelocity = 0; }
    else playback = FreshPlayback(index);
    cfg.scene = index;
    configDirty = true;
    page = Page::Scene;
    settingsOpen = false;
    toast.show(SceneName(cfg.scene) + "   (ESC = menu, R = recenter)", 6);
}

void AppState::setStrength(double value) {
    cfg.strength = std::clamp(value, 0.1, 1.5);
    configDirty = true;
}

void AppState::setFov(double deg, bool diagonal) {
    cfg.fovDeg = std::clamp(deg, 20.0, 160.0);
    cfg.fovDiagonal = diagonal;
    paramsDirty = true;
    configDirty = true;
}

void AppState::setTilt(double deg) {
    cfg.camTiltDeg = std::clamp(deg, -kMaxTiltDeg, kMaxTiltDeg);
    paramsDirty = true;
    configDirty = true;
}

void AppState::setWebcamView(WebcamView view) {
    cfg.webcamView = int(view);
    configDirty = true;
}

void AppState::togglePhysicalAlignment() {
    physicalAlignment = !physicalAlignment;
    toast.show(physicalAlignment ? (cfg.photoDone ? "True geometry (from the screen photo)"
                                                  : "True geometry with a GUESSED webcam position: do the screen photo in Settings")
                                 : "Recentered on your sitting position (R to recenter)", 4);
}

void AppState::openSettings() {
    if (settingsOpen) return;
    pageUnderSettings = page;
    settingsOpen = true;
}

void AppState::closeSettings() { settingsOpen = false; }

void AppState::openPage(Page p) {
    settingsOpen = false;
    page = p;
}

void AppState::backToSettings() {
    page = pageUnderSettings;
    settingsOpen = true;
}

std::string AppState::fovLabel() const {
    if (!(cfg.fovDeg > 10)) return Fmt("%.1f deg horizontal (default; not entered)", kDefaultHFovDeg);
    return cfg.fovDiagonal ? Fmt("%.1f deg diagonal = %.1f deg horizontal", cfg.fovDeg, cfg.hfovDeg())
                           : Fmt("%.1f deg horizontal", cfg.fovDeg);
}

} // namespace tess
