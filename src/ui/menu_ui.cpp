#include "ui/menu_ui.h"

#include "render/scenes.h"
#include "ui/ui_widgets.h"

#include <algorithm>

namespace tess {

bool MenuUi::update(AppState& st, const UiFrame&) {
    if (st.page != Page::StartMenu && st.page != Page::SceneSelect) return false;
    if (!st.settingsOpen && IsKeyPressed(KEY_ESCAPE)) {
        if (st.page == Page::SceneSelect) st.page = Page::StartMenu;
        else st.quit = true;
    }
    return true;   // the menu pages own the input
}

void MenuUi::draw(AppState& st, const UiFrame& f) {
    if (st.page != Page::StartMenu && st.page != Page::SceneSelect) return;
    DrawMenuBackground();
    const bool interactive = !st.settingsOpen;
    const float sw = float(f.geo.sw), sh = float(f.geo.sh);
    const float buttonWidth = std::min(460.0f, sw - 80.0f);
    const float bx = sw / 2 - buttonWidth / 2;

    if (st.page == Page::StartMenu) {
        DrawCenteredText("Tesseract", 64, sh * 0.25f, theme::kInk);
        DrawCenteredText("Head-tracked depth window", 18, sh * 0.25f + 82, theme::kMuted);
        const float h = 66, gap = 20;
        float y = sh * 0.44f;
        if (Button({bx, y, buttonWidth, h}, "Select Scene", interactive, 20)) st.page = Page::SceneSelect;
        y += h + gap;
        if (Button({bx, y, buttonWidth, h}, "Settings", interactive, 20)) st.openSettings();
        y += h + gap;
        if (Button({bx, y, buttonWidth, h}, "Exit Program", interactive, 20)) st.quit = true;
        DrawCenteredText(st.cameraActive ? "Camera connected" : st.cameraStarting ? "Opening camera..." : "Camera off (Settings)",
                         14, sh - 36, st.cameraActive ? theme::kAccent : theme::kMuted);
        return;
    }

    DrawCenteredText("Select a Scene", 38, sh * 0.16f, theme::kInk);
    DrawCenteredText("Look through the screen like a window. ESC in a scene returns here.", 18, sh * 0.16f + 56, theme::kMuted);
    const int n = SceneCount();
    const float h = 58, gap = 14;
    const float total = n * h + (n - 1) * gap + 24 + 50;
    float y = std::max(sh * 0.30f, (sh - total) / 2);
    for (int i = 0; i < n; ++i) {
        const Rectangle r{bx, y, buttonWidth, h};
        if (i == st.cfg.scene) DrawRectangleRoundedLines({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, 0.2f, 10, Fade(theme::kAccent, 0.5f));
        if (Button(r, Scenes()[i].name, interactive, 20)) st.enterScene(i);
        y += h + gap;
    }
    y += 24;
    if (Button({bx, y, buttonWidth, 50}, "Back to Main Menu", interactive, 18)) st.page = Page::StartMenu;
}

void SceneOverlayUi::draw(AppState& st, const UiFrame& f) {
    if (st.page != Page::Scene || st.settingsOpen || f.mouseIdleS > 2.5) return;
    const float a = float(std::clamp(1.0 - (f.mouseIdleS - 1.5), 0.0, 1.0));   // fade out over the last second
    const Rectangle r{float(f.geo.sw) - 136, 12, 124, 36};
    DrawRectangleRounded(r, 0.2f, 8, Fade(theme::kPanel, 0.85f * a));
    if (a > 0.5f && Button(r, "Settings", true, 16)) st.openSettings();
    else if (a <= 0.5f) DrawText("Settings", int(r.x + 26), int(r.y + 9), 16, Fade(theme::kInk, a));
}

} // namespace tess
