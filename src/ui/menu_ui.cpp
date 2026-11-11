#include "ui/menu_ui.h"

#include "render/scenes.h"
#include "ui/file_dialog.h"
#include "ui/ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tess {
namespace {

constexpr size_t kMaxDescription = 400;
constexpr double kDoubleClickS = 0.35;

// Word-wraps text to the given pixel width (raylib's default font).
std::vector<std::string> Wrap(const std::string& text, int fontSize, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    auto fits = [&](const std::string& s) { return MeasureText(s.c_str(), fontSize) <= width; };
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string joined = line.empty() ? word : line + " " + word;
        if (fits(joined)) { line = joined; word.clear(); return; }
        if (!line.empty()) { lines.push_back(line); line.clear(); }
        while (!fits(word)) {   // a single word wider than the box: break it
            size_t n = word.size();
            while (n > 1 && !fits(word.substr(0, n))) --n;
            lines.push_back(word.substr(0, n));
            word.erase(0, n);
        }
        line = word;
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ') flushWord();
        else word.push_back(ch);
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

float DrawWrapped(const std::string& text, Rectangle box, int fontSize, Color c) {
    float y = box.y;
    for (const std::string& line : Wrap(text, fontSize, box.width)) {
        if (y + fontSize > box.y + box.height) break;
        DrawText(line.c_str(), int(box.x), int(y), fontSize, c);
        y += fontSize + 6;
    }
    return y;
}

// Shortens a label with "..." so it fits the given width.
std::string Ellipsize(const std::string& s, int fontSize, float width) {
    if (MeasureText(s.c_str(), fontSize) <= width) return s;
    std::string out = s;
    while (!out.empty() && MeasureText((out + "...").c_str(), fontSize) > width) out.pop_back();
    return out + "...";
}

// Selected row: a thick accent outline (the blue box in the mockup).
void DrawSelection(Rectangle r) {
    DrawRectangleRoundedLinesEx({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 0.2f, 10, 3.0f, theme::kAccent);
}

} // namespace

bool MenuUi::update(AppState& st, const UiFrame&) {
    if (st.page != Page::StartMenu && st.page != Page::SceneSelect) return false;
    if (!st.settingsOpen && IsKeyPressed(KEY_ESCAPE)) {
        if (st.page == Page::SceneSelect && panel_ != Panel::Info) panel_ = Panel::Info;   // close the sub-panel first
        else if (st.page == Page::SceneSelect) st.page = Page::StartMenu;
        else st.quit = true;
    }
    return true;   // the menu pages own the input
}

void MenuUi::draw(AppState& st, const UiFrame& f) {
    if (st.page != Page::StartMenu && st.page != Page::SceneSelect) return;
    if (st.page == Page::SceneSelect) { drawSceneSelect(st, f); return; }

    DrawMenuBackground();
    const bool interactive = !st.settingsOpen;
    const float sw = float(f.geo.sw), sh = float(f.geo.sh);
    const float buttonWidth = std::min(460.0f, sw - 80.0f);
    const float bx = sw / 2 - buttonWidth / 2;
    DrawCenteredText("Tesseract", 64, sh * 0.25f, theme::kInk);
    DrawCenteredText("Head-tracked depth window", 18, sh * 0.25f + 82, theme::kMuted);
    const float h = 66, gap = 20;
    float y = sh * 0.44f;
    if (Button({bx, y, buttonWidth, h}, "Select Scene", interactive, 20)) st.openSceneSelect();
    y += h + gap;
    if (Button({bx, y, buttonWidth, h}, "Settings", interactive, 20)) st.openSettings();
    y += h + gap;
    if (Button({bx, y, buttonWidth, h}, "Exit Program", interactive, 20)) st.quit = true;
    DrawCenteredText(st.cameraActive ? "Camera connected" : st.cameraStarting ? "Opening camera..." : "Camera off (Settings)",
                     14, sh - 36, st.cameraActive ? theme::kAccent : theme::kMuted);
}

void MenuUi::drawSceneSelect(AppState& st, const UiFrame& f) {
    const float sw = float(f.geo.sw), sh = float(f.geo.sh);
    if (st.previewScene < 0) DrawMenuBackground();   // nothing picked yet: the default background

    // After an import: preview it with the description box open.
    if (st.describeScene >= 0) {
        st.previewSceneAt(st.describeScene);
        panelScene_ = st.describeScene;
        text_ = SceneDescription(st.describeScene);
        panel_ = Panel::EditDescription;
        st.describeScene = -1;
    }
    if (panelScene_ != st.previewScene) { panel_ = Panel::Info; panelScene_ = st.previewScene; deleteArmed_ = false; }

    const float leftW = std::clamp(sw * 0.27f, 240.0f, 340.0f);
    const Rectangle left{16, 16, leftW, sh - 76};   // leaves room for the toast at the bottom
    DrawRectangleRounded(left, 0.04f, 10, Fade(theme::kPanel, 0.92f));
    drawSceneList(st, left);

    if (st.previewScene >= 0) {
        drawScenePanel(st, f, left.x + left.width);
        const char* hint = "Move the mouse to look around. Open the scene to use head tracking.";
        const float cx = (left.x + left.width + sw) / 2;
        const int tw = MeasureText(hint, 15);
        DrawRectangleRounded({cx - tw / 2.0f - 12, 18, tw + 24.0f, 28}, 0.5f, 8, Fade(theme::kPanel, 0.75f));
        DrawText(hint, int(cx - tw / 2.0f), 25, 15, theme::kInk);
    } else {
        const float cx = (left.x + left.width + sw) / 2;
        const char* title = "Select a Scene";
        const char* sub = "Pick a scene on the left to preview it.";
        DrawText(title, int(cx - MeasureText(title, 38) / 2.0f), int(sh * 0.40f), 38, theme::kInk);
        DrawText(sub, int(cx - MeasureText(sub, 18) / 2.0f), int(sh * 0.40f + 54), 18, theme::kMuted);
    }
}

void MenuUi::drawSceneList(AppState& st, Rectangle area) {
    const bool interactive = !st.settingsOpen && panel_ != Panel::EditDescription;
    const int n = SceneCount(), builtIn = BuiltInSceneCount();
    const float pad = 16, rowH = 52, gap = 10, labelH = 32;
    const float x = area.x + pad, w = area.width - 2 * pad;
    DrawText("SCENES", int(x), int(area.y + 18), 14, theme::kAccent);

    // Rows: built-in scenes, "YOUR MODELS", imported scenes, then "Load a scene...".
    const Rectangle view{area.x + 4, area.y + 44, area.width - 8, area.height - 44 - 66};
    const float contentH = (n + 1) * (rowH + gap) + (n > builtIn ? labelH : 0) + 8;
    const float maxScroll = std::max(0.0f, contentH - view.height);
    const Vector2 mouse = GetMousePosition();
    const bool mouseInView = CheckCollisionPointRec(mouse, view);
    if (interactive && mouseInView) scroll_ -= GetMouseWheelMove() * 60.0f;
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);

    BeginScissorMode(int(view.x), int(view.y), int(view.width), int(view.height));
    float y = view.y + 6 - scroll_;
    for (int i = 0; i < n; ++i) {
        if (i == builtIn) {
            DrawText("YOUR MODELS", int(x), int(y + 10), 13, theme::kMuted);
            y += labelH;
        }
        const Rectangle r{x, y, w, rowH};
        if (i == st.previewScene) DrawSelection(r);
        const std::string name = Ellipsize(SceneName(i), 18, w - 24);
        if (Button(r, name.c_str(), interactive, 18) && mouseInView) {
            const bool again = lastClickRow_ == i && GetTime() - lastClickTime_ < kDoubleClickS;
            st.previewSceneAt(i);
            if (again) st.enterScene(i);
            lastClickRow_ = i;
            lastClickTime_ = GetTime();
        }
        y += rowH + gap;
    }
    if (Button({x, y, w, rowH}, "+  Load a scene...", interactive, 18) && mouseInView) {
        const std::string path = OpenModelFileDialog(GetWindowHandle());
        if (!path.empty()) st.importModel(path);
    }
    EndScissorMode();
    if (maxScroll > 0) {
        const float barH = view.height * view.height / contentH;
        const float barY = view.y + (view.height - barH) * (scroll_ / maxScroll);
        DrawRectangleRounded({area.x + area.width - 7, barY, 4, barH}, 1, 6, Fade(theme::kMuted, 0.6f));
    }
    if (Button({x, area.y + area.height - 56, w, 42}, "Back to Main Menu", interactive, 17)) st.page = Page::StartMenu;
}

void MenuUi::drawScenePanel(AppState& st, const UiFrame& f, float left) {
    const float sw = float(f.geo.sw), sh = float(f.geo.sh);
    const int scene = st.previewScene;
    const bool imported = IsImportedScene(scene);
    const int lib = ImportedIndex(scene);
    const bool interactive = !st.settingsOpen;

    const float w = std::min(430.0f, sw - left - 48);
    const float h = panel_ == Panel::Info ? 250.0f : panel_ == Panel::Settings ? 330.0f : 300.0f;
    const Rectangle box{sw - w - 16, sh - h - 60, w, h};
    const float x = box.x + 22, cw = box.width - 44;
    DrawRectangleRounded(box, 0.06f, 10, Fade(theme::kPanel, 0.92f));
    const std::string name = SceneName(scene);
    BeginScissorMode(int(x), int(box.y + 16), int(cw), 32);
    DrawText(name.c_str(), int(x), int(box.y + 18), 26, theme::kInk);
    EndScissorMode();

    if (panel_ == Panel::Info) {
        const std::string desc = SceneDescription(scene);
        DrawWrapped(desc.empty() ? std::string("No description.") : desc, {x, box.y + 60, cw, box.height - 60 - 70}, 17,
                    desc.empty() ? theme::kMuted : theme::kInk);
        const float by = box.y + box.height - 58;
        const float openW = imported ? cw * 0.58f : cw;
        if (Button({x, by, openW, 42}, "Open scene", interactive, 19)) st.enterScene(scene);
        if (imported && Button({x + openW + 10, by, cw - openW - 10, 42}, "Scene settings", interactive, 16)) {
            panel_ = Panel::Settings;
            deleteArmed_ = false;
        }
        return;
    }

    if (panel_ == Panel::Settings) {
        SectionLabel(x, box.y + 58, "SCENE SETTINGS");
        ImportedSceneInfo info = ImportedModelInfo(lib);
        bool changed = false;
        float y = box.y + 82;
        if (Toggle({x, y, cw, 36}, info.allowRotation ? "Allow rotation: on (drag to spin)" : "Allow rotation: off",
                   info.allowRotation, interactive)) { info.allowRotation = !info.allowRotation; changed = true; }
        y += 44;
        if (Toggle({x, y, cw, 36}, info.backdrop ? "Studio backdrop: on" : "Studio backdrop: off", info.backdrop,
                   interactive)) { info.backdrop = !info.backdrop; changed = true; }
        y += 44;
        if (Toggle({x, y, cw, 36}, info.autoplay ? "Auto-play animations: on" : "Auto-play animations: off",
                   info.autoplay, interactive)) {
            info.autoplay = !info.autoplay;
            st.playback.playing = info.autoplay;
            changed = true;
        }
        if (changed && !SetImportedModelInfo(lib, info)) st.toast.show("Could not save models/library.yml", 5);
        y += 54;
        const float half = (cw - 10) / 2;
        if (Button({x, y, half, 38}, "Edit description", interactive, 16)) {
            text_ = info.description;
            panel_ = Panel::EditDescription;
        }
        if (deleteArmed_ && GetTime() > deleteUntil_) deleteArmed_ = false;
        const Rectangle del{x + half + 10, y, half, 38};
        if (Button(del, deleteArmed_ ? "Click again to delete" : "Delete scene", interactive, 15)) {
            if (deleteArmed_) { st.removeScene = scene; deleteArmed_ = false; panel_ = Panel::Info; }
            else { deleteArmed_ = true; deleteUntil_ = GetTime() + 3.0; }
        }
        if (deleteArmed_) DrawRectangleRoundedLines(del, 0.16f, 10, theme::kWarn);
        if (Button({x, box.y + box.height - 54, cw, 38}, "Done", interactive, 16)) panel_ = Panel::Info;
        return;
    }

    // Edit description.
    SectionLabel(x, box.y + 58, "DESCRIPTION (OPTIONAL)");
    if (interactive) editText();
    const Rectangle field{x, box.y + 82, cw, 140};
    DrawRectangleRounded(field, 0.06f, 8, Fade(BLACK, 0.35f));
    DrawRectangleRoundedLines(field, 0.06f, 8, theme::kAccent);
    const Rectangle inner{field.x + 10, field.y + 10, field.width - 20, field.height - 20};
    const std::vector<std::string> lines = Wrap(text_, 17, inner.width);
    float ty = inner.y;
    const size_t visible = size_t(inner.height / 23);
    const size_t first = lines.size() > visible ? lines.size() - visible : 0;   // keep the end (caret) in view
    for (size_t i = first; i < lines.size(); ++i, ty += 23) DrawText(lines[i].c_str(), int(inner.x), int(ty), 17, theme::kInk);
    if (std::fmod(GetTime(), 1.0) < 0.5) {   // caret at the end
        const std::string last = lines.empty() ? "" : lines.back();
        const float cy = lines.empty() ? inner.y : ty - 23;
        DrawRectangle(int(inner.x + MeasureText(last.c_str(), 17) + 2), int(cy), 2, 18, theme::kAccent);
    }
    if (text_.empty()) DrawText("Type a description, or Skip.", int(inner.x + 8), int(inner.y), 17, theme::kMuted);
    DrawText(TextFormat("%d / %d   Enter = save, Esc = cancel", int(text_.size()), int(kMaxDescription)), int(x),
             int(field.y + field.height + 8), 13, theme::kMuted);

    const float by = box.y + box.height - 54, half = (cw - 10) / 2;
    const bool save = Button({x, by, half, 38}, "Save", interactive, 16) || (interactive && IsKeyPressed(KEY_ENTER));
    if (save) {
        ImportedSceneInfo info = ImportedModelInfo(lib);
        info.description = text_;
        if (!SetImportedModelInfo(lib, info)) st.toast.show("Could not save models/library.yml", 5);
        panel_ = Panel::Info;
    }
    if (Button({x + half + 10, by, half, 38}, text_.empty() ? "Skip" : "Cancel", interactive, 16)) panel_ = Panel::Info;
}

void MenuUi::editText() {
    for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
        if (c >= 32 && c < 127 && text_.size() < kMaxDescription) text_.push_back(char(c));   // the font is ASCII
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text_.empty()) text_.pop_back();
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
        if (const char* clip = GetClipboardText()) {
            for (const char* p = clip; *p && text_.size() < kMaxDescription; ++p) {
                const char ch = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p;
                if (ch >= 32 && ch < 127) text_.push_back(ch);
            }
        }
    }
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
