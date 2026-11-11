// On an imported-model scene: click and drag to spin the model (double-click resets), animation controls
// (play / pause, clip, timeline slider) along the bottom while the mouse is moving, Space to play / pause, and a
// notice if the model's file no longer loads.
#pragma once
#include "ui/ui_layer.h"

namespace tess {

class ModelControlsUi : public UiLayer {
public:
    bool update(AppState& state, const UiFrame& frame) override;
    void draw(AppState& state, const UiFrame& frame) override;

private:
    bool dragging_ = false;
    double lastClick_ = 0;
};

} // namespace tess
