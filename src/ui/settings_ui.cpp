#include "ui/settings_ui.h"

#include "core/text.h"

#include <algorithm>
#include <cmath>

namespace tess {

bool SettingsUi::update(AppState& st, const UiFrame&) {
    if (!st.settingsOpen) return false;
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (fovBox_.active) fovBox_.active = false;
        else st.closeSettings();
    }
    return true;
}

void SettingsUi::draw(AppState& st, const UiFrame& f) {
    if (!st.settingsOpen) return;
    Config& cfg = st.cfg;
    const int sw = f.geo.sw, sh = f.geo.sh;
    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.65f));

    const float w = std::min(640.0f, sw - 24.0f), h = std::min(720.0f, sh - 24.0f);
    const float x = (sw - w) / 2, y = (sh - h) / 2;
    const float col = x + 36, cw = w - 72;
    DrawRectangleRounded({x, y, w, h}, 0.04f, 10, theme::kPanel);
    DrawText("TESSERACT SETTINGS", int(col), int(y + 24), 24, theme::kInk);
    DrawText("Changes are saved automatically.", int(col), int(y + 56), 14, theme::kMuted);

    // ---- parallax ----
    SectionLabel(col, y + 86, "PARALLAX");
    double strength = cfg.strength;
    if (Slider(col, y + 108, cw, "Parallax strength (1.00 = physically exact)", strength, 0.1, 1.5, "%.2f")) st.setStrength(strength);
    {
        const float tw = (cw - 3 * 8) / 4, ty = y + 158;
        if (Toggle({col, ty, tw, 34}, "True geometry", st.physicalAlignment)) st.togglePhysicalAlignment();
        if (Toggle({col + (tw + 8), ty, tw, 34}, "Prediction", cfg.prediction)) { cfg.prediction = !cfg.prediction; st.requestSave(); }
        if (Toggle({col + 2 * (tw + 8), ty, tw, 34}, "HUD", cfg.showHud)) { cfg.showHud = !cfg.showHud; st.requestSave(); }
        if (Toggle({col + 3 * (tw + 8), ty, tw, 34}, "Fullscreen", st.fullscreen)) st.fullscreen = !st.fullscreen;
    }

    // ---- camera ----
    SectionLabel(col, y + 208, "CAMERA");
    {
        const int n = int(st.cameras.size());
        if (selectedCamera_ < 0 || selectedCamera_ >= n) {
            selectedCamera_ = 0;
            for (int i = 0; i < n; ++i) if (st.cameras[i].index == cfg.cameraIndex) selectedCamera_ = i;
        }
        const float ry = y + 230;
        if (Button({col, ry, 36, 34}, "<", n > 1, 18)) selectedCamera_ = (selectedCamera_ + n - 1) % n;
        if (Button({col + cw - 36, ry, 36, 34}, ">", n > 1, 18)) selectedCamera_ = (selectedCamera_ + 1) % n;
        const CameraInfo& cam = st.cameras[selectedCamera_];
        const std::string name = Fmt("%s   (index %d)", cam.name.c_str(), cam.index);
        BeginScissorMode(int(col + 46), int(ry), int(cw - 92), 34);
        DrawText(name.c_str(), int(col + 46 + (cw - 92 - MeasureText(name.c_str(), 17)) / 2), int(ry + 9), 17, theme::kInk);
        EndScissorMode();

        const bool sameAsActive = st.cameraActive && cam.index == cfg.cameraIndex;
        const char* label = st.cameraStarting ? "Starting..." : sameAsActive ? "Stop camera"
                          : st.cameraActive ? "Switch camera" : "Start camera";
        const float by = y + 274;
        if (Button({col, by, 170, 34}, label, !st.cameraStarting, 16)) st.cameraRequest = sameAsActive ? -1 : cam.index;
        if (Button({col + 182, by, 130, 34}, "Refresh list", !st.cameraStarting, 16)) { st.refreshCameras = true; selectedCamera_ = -1; }
        BeginScissorMode(int(col + 324), int(by), int(cw - 324), 34);
        DrawText(st.cameraStatus.c_str(), int(col + 324), int(by + 10), 13, st.cameraActive ? theme::kAccent : theme::kMuted);
        EndScissorMode();
    }

    // ---- webcam view ----
    SectionLabel(col, y + 324, "WEBCAM VIEW");
    {
        const float tw = (cw - 2 * 8) / 3, ty = y + 346;
        const WebcamView v = st.webcamView();
        if (Toggle({col, ty, tw, 34}, "Off", v == WebcamView::Off)) st.setWebcamView(WebcamView::Off);
        if (Toggle({col + (tw + 8), ty, tw, 34}, "Separate window", v == WebcamView::Window)) st.setWebcamView(WebcamView::Window);
        if (Toggle({col + 2 * (tw + 8), ty, tw, 34}, "Picture-in-picture", v == WebcamView::PictureInPicture))
            st.setWebcamView(WebcamView::PictureInPicture);
    }

    // ---- calibration ----
    SectionLabel(col, y + 396, "CALIBRATION");
    {
        const float ry = y + 418;
        DrawText("Webcam FOV", int(col), int(ry + 8), 16, theme::kInk);
        const double shownFov = cfg.fovDeg > 10 ? cfg.fovDeg
                              : (cfg.fovDiagonal ? 2.0 * std::atan(std::tan(kDefaultHFovDeg * kPi / 360.0) * std::hypot(double(kCamW), double(kCamH)) / kCamW) * 180.0 / kPi
                                                 : kDefaultHFovDeg);
        if (fovBox_.draw({col + 112, ry, 96, 34}, shownFov, 1)) {
            const double v = fovBox_.field.value();
            if (v > 20 && v < 160) { st.setFov(v, cfg.fovDiagonal); st.toast.show("Webcam field of view: " + st.fovLabel(), 4); }
            else st.toast.show("Enter a field of view between 20 and 160 degrees", 4);
        }
        if (Toggle({col + 216, ry, 118, 34}, cfg.fovDiagonal ? "Diagonal" : "Horizontal", true)) {
            cfg.fovDiagonal = !cfg.fovDiagonal;
            if (cfg.fovDeg > 10) st.paramsDirty = true;
            st.requestSave();
        }
        const std::string readout = f.snap.tracking
            ? Fmt("Reads %.0f mm now. Too short: lower FOV. Too long: raise it.", f.snap.rawZ)
            : std::string(cfg.fovDeg > 10 ? "From the webcam spec sheet (usually diagonal)." : "Not entered: using 70 deg horizontal.");
        BeginScissorMode(int(col + 346), int(ry), int(cw - 346), 34);
        DrawText(readout.c_str(), int(col + 346), int(ry + 3), 12, theme::kMuted);
        DrawText(cfg.photoDone ? Fmt("Screen photo done: %.0f x %.0f mm", cfg.screenWmm, cfg.screenHmm).c_str()
                               : ("Screen photo not done: using " + f.dims.source).c_str(),
                 int(col + 346), int(ry + 19), 12, cfg.photoDone ? theme::kAccent : theme::kWarn);
        EndScissorMode();

        double tilt = cfg.camTiltDeg;
        if (Slider(col, y + 464, cw, "Webcam tilt (degrees; + = pointing down toward you)", tilt, -kMaxTiltDeg, kMaxTiltDeg, "%+.0f"))
            st.setTilt(tilt);

        const float by = y + 516;
        if (Button({col, by, 300, 38}, "Screen photo: corners + webcam lens...", true, 16)) st.openPage(Page::Calibration);
        if (Button({col + 312, by, cw - 312, 38}, "Pen-tip test...", true, 16)) st.openPage(Page::PenTipTest);
    }

    // ---- imported models ----
    SectionLabel(col, y + 566, "IMPORTED MODELS");
    {
        const float tw = (cw - 8) / 2, ty = y + 588;
        if (Toggle({col, ty, tw, 34}, "Auto-play animations", cfg.modelAutoplay)) {
            cfg.modelAutoplay = !cfg.modelAutoplay;
            st.requestSave();
        }
        if (Toggle({col + tw + 8, ty, tw, 34}, "Studio backdrop", cfg.modelBackdrop)) {
            cfg.modelBackdrop = !cfg.modelBackdrop;
            st.requestSave();
        }
    }

    // ---- bottom ----
    const float by = y + h - 58;
    if (Button({col, by, 160, 38}, "Reset defaults", true, 16)) {
        cfg.resetTuning();
        st.paramsDirty = true;
        st.requestSave();
        st.toast.show("Strength, FOV, tilt, prediction and HUD reset (screen photo and camera kept)", 5);
    }
    if (Button({col + cw - 160, by, 160, 38}, "Back", true, 16)) st.closeSettings();
    DrawText("ESC closes this panel", int(col + 176), int(by + 12), 13, theme::kMuted);
}

} // namespace tess
