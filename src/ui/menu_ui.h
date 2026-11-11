// The start menu (Select Scene / Settings / Exit), the scene selector, and the small Settings button that
// appears in a scene when the mouse moves.
//
// Scene selector: scene names on the left (plus "Load a scene..." to import a model). Clicking a name previews the
// scene behind the menu with mouse parallax; the panel on the right shows its description and Open scene.
// Imported scenes also have Scene settings: allow rotation, studio backdrop, auto-play, edit description, delete.
#pragma once
#include "ui/ui_layer.h"

#include <string>

namespace tess {

// Pages StartMenu and SceneSelect.
class MenuUi : public UiLayer {
public:
    bool update(AppState& state, const UiFrame& frame) override;
    void draw(AppState& state, const UiFrame& frame) override;

private:
    enum class Panel { Info, Settings, EditDescription };

    void drawSceneSelect(AppState& st, const UiFrame& f);
    void drawSceneList(AppState& st, Rectangle area);
    void drawScenePanel(AppState& st, const UiFrame& f, float left);
    void editText();                     // typing into text_ while the description box is open

    float scroll_ = 0;                   // scene list scroll offset, pixels
    Panel panel_ = Panel::Info;
    int panelScene_ = -1;                // the scene the panel was opened for
    std::string text_;                   // description being edited
    bool deleteArmed_ = false;           // Delete clicked once (click again within 3 s to confirm)
    double deleteUntil_ = 0;
    int lastClickRow_ = -1;              // double-click on a name opens the scene
    double lastClickTime_ = 0;
};

// On the Scene page: a Settings button in the top-right corner while the mouse is moving.
class SceneOverlayUi : public UiLayer {
public:
    void draw(AppState& state, const UiFrame& frame) override;
};

} // namespace tess
