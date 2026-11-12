// The Settings panel: a modal overlay available from the start menu and inside a scene.
//   Parallax     strength slider; true geometry / prediction / HUD / fullscreen toggles
//   Camera       pick any webcam on the machine, start / switch / stop
//   Webcam view  off, separate window, picture-in-picture
//   Calibration  webcam field of view, webcam tilt, screen photo (corners + lens)
// Everything saves to tesseract_calibration.yml as it changes.
#pragma once
#include "ui/ui_layer.h"
#include "ui/ui_widgets.h"

namespace tess {

class SettingsUi : public UiLayer {
public:
    bool update(AppState& state, const UiFrame& frame) override;
    void draw(AppState& state, const UiFrame& frame) override;

private:
    int selectedCamera_ = -1;   // position in state.cameras; -1 = not chosen yet
    NumberBox fovBox_;
};

} // namespace tess
