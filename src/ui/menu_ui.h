// The start menu (Select Scene / Settings / Exit), the scene picker (with Import 3D model and Delete for imported
// scenes), and the small Settings button that appears in a scene when the mouse moves.
#pragma once
#include "ui/ui_layer.h"

namespace tess {

// Pages StartMenu and SceneSelect.
class MenuUi : public UiLayer {
public:
    bool update(AppState& state, const UiFrame& frame) override;
    void draw(AppState& state, const UiFrame& frame) override;

private:
    float scroll_ = 0;          // scene list scroll offset, pixels
    int pendingDelete_ = -1;    // scene index whose Delete was clicked once (click again to confirm)
    double pendingUntil_ = 0;
};

// On the Scene page: a Settings button in the top-right corner while the mouse is moving.
class SceneOverlayUi : public UiLayer {
public:
    void draw(AppState& state, const UiFrame& frame) override;
};

} // namespace tess
