// The start menu (Select Scene / Settings / Exit), the scene picker, and the small Settings button that
// appears in a scene when the mouse moves.
#pragma once
#include "ui/ui_layer.h"

namespace tess {

// Pages StartMenu and SceneSelect.
class MenuUi : public UiLayer {
public:
    bool update(AppState& state, const UiFrame& frame) override;
    void draw(AppState& state, const UiFrame& frame) override;
};

// On the Scene page: a Settings button in the top-right corner while the mouse is moving.
class SceneOverlayUi : public UiLayer {
public:
    void draw(AppState& state, const UiFrame& frame) override;
};

} // namespace tess
