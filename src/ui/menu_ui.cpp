#include "ui/menu_ui.h"

#include "render/scenes.h"
#include "ui/file_dialog.h"
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

    // ---- scene picker: built-in scenes, then imported models (scrolls when it does not fit) ----
    const float titleY = sh * 0.10f;
    DrawCenteredText("Select a Scene", 38, titleY, theme::kInk);
    DrawCenteredText("Look through the screen like a window. ESC in a scene returns here.", 18, titleY + 56, theme::kMuted);

    const int n = SceneCount(), builtIn = BuiltInSceneCount();
    const float h = 54, gap = 12, labelH = 34, deleteW = 84;
    const float contentH = n * (h + gap) - gap + (n > builtIn ? labelH : 0);
    const Rectangle view{bx - 8, titleY + 96, buttonWidth + 16, std::max(h, sh - 170 - (titleY + 96))};
    const float maxScroll = std::max(0.0f, contentH + 8 - view.height);
    const Vector2 mouse = GetMousePosition();
    const bool mouseInView = CheckCollisionPointRec(mouse, view);
    if (interactive && mouseInView) scroll_ -= GetMouseWheelMove() * 60.0f;
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
    if (pendingDelete_ >= 0 && GetTime() > pendingUntil_) pendingDelete_ = -1;

    BeginScissorMode(int(view.x), int(view.y), int(view.width), int(view.height));
    float y = view.y + 4 - scroll_;
    for (int i = 0; i < n; ++i) {
        if (i == builtIn) {
            DrawText("YOUR MODELS", int(bx), int(y + 12), 14, theme::kAccent);
            y += labelH;
        }
        const bool imported = i >= builtIn;
        const Rectangle r{bx, y, imported ? buttonWidth - deleteW - 8 : buttonWidth, h};
        if (i == st.cfg.scene) DrawRectangleRoundedLines({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, 0.2f, 10, Fade(theme::kAccent, 0.5f));
        const std::string name = SceneName(i);
        if (Button(r, name.c_str(), interactive, 20) && mouseInView) st.enterScene(i);
        if (imported) {
            const bool confirm = pendingDelete_ == i;
            const Rectangle d{bx + buttonWidth - deleteW, y, deleteW, h};
            if (Button(d, confirm ? "Sure?" : "Delete", interactive, 16) && mouseInView) {
                if (confirm) { st.removeScene = i; pendingDelete_ = -1; }
                else { pendingDelete_ = i; pendingUntil_ = GetTime() + 3.0; }
            }
            if (confirm) DrawRectangleRoundedLines(d, 0.16f, 10, theme::kWarn);
        }
        y += h + gap;
    }
    EndScissorMode();
    if (maxScroll > 0) {   // scroll bar
        const float barH = view.height * view.height / (contentH + 8);
        const float barY = view.y + (view.height - barH) * (scroll_ / maxScroll);
        DrawRectangleRounded({view.x + view.width + 6, barY, 5, barH}, 1, 6, Fade(theme::kMuted, 0.6f));
    }

    const float by = sh - 150;
    const float half = (buttonWidth - 12) / 2;
    if (Button({bx, by, half, 50}, "Import 3D model...", interactive, 18)) {
        const std::string path = OpenModelFileDialog(GetWindowHandle());
        if (!path.empty()) st.importModel(path);
    }
    if (Button({bx + half + 12, by, half, 50}, "Back to Main Menu", interactive, 18)) st.page = Page::StartMenu;
    DrawCenteredText("Or drag a .glb / .gltf / .obj file onto this window.", 15, by + 64, theme::kMuted);
    DrawCenteredText("From Blender: File > Export > glTF 2.0 (.glb), tick Include > Cameras to keep your camera.", 15,
                     by + 86, theme::kMuted);
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
