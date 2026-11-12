#include "ui/ui_widgets.h"

#include "core/text.h"

#include <algorithm>
#include <cmath>

namespace tess {
namespace {
// The slider being dragged (so the pointer may leave the track while the button is held).
int gActiveSlider = 0;
int SliderId(float x, float y) { return int(x) * 7919 + int(y) * 31 + 1; }
} // namespace

bool Button(Rectangle r, const char* label, bool enabled, int fontSize) {
    const bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.16f, 10, hover ? theme::kButtonHover : theme::kButton);
    DrawRectangleRoundedLines(r, 0.16f, 10, hover ? theme::kAccent : theme::kButtonBorder);
    const int tw = MeasureText(label, fontSize);
    DrawText(label, int(r.x + (r.width - tw) / 2), int(r.y + (r.height - fontSize) / 2), fontSize,
             enabled ? theme::kInk : theme::kMuted);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool Toggle(Rectangle r, const char* label, bool on, bool enabled, int fontSize) {
    const bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.16f, 10, on ? theme::kOn : hover ? theme::kButtonHover : theme::kButton);
    DrawRectangleRoundedLines(r, 0.16f, 10, on || hover ? theme::kAccent : theme::kButtonBorder);
    const int tw = MeasureText(label, fontSize);
    DrawText(label, int(r.x + (r.width - tw) / 2), int(r.y + (r.height - fontSize) / 2), fontSize,
             enabled ? theme::kInk : theme::kMuted);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool Slider(float x, float y, float w, const char* label, double& value, double low, double high, const char* fmt,
            bool enabled) {
    DrawText(label, int(x), int(y), 16, enabled ? theme::kInk : theme::kMuted);
    const std::string shown = Fmt(fmt, value);
    DrawText(shown.c_str(), int(x + w - MeasureText(shown.c_str(), 16)), int(y), 16, theme::kAccent);
    const Rectangle track{x, y + 31, w, 8};
    const int id = SliderId(x, y);
    bool changed = false;
    if (enabled) {
        const Rectangle hit{track.x - 8, track.y - 12, track.width + 16, 32};
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), hit)) gActiveSlider = id;
        if (gActiveSlider == id) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                const double nv = low + (high - low) * std::clamp((GetMouseX() - track.x) / track.width, 0.0f, 1.0f);
                if (nv != value) { value = nv; changed = true; }
            } else {
                gActiveSlider = 0;
            }
        }
    }
    const float amount = float(std::clamp((value - low) / (high - low), 0.0, 1.0));
    DrawRectangleRounded(track, 1, 8, theme::kTrack);
    DrawRectangleRounded({track.x, track.y, track.width * amount, track.height}, 1, 8, theme::kAccent);
    DrawCircle(int(track.x + track.width * amount), int(track.y + 4), 8, theme::kInk);
    return changed;
}

void NumberField::set(double v, int decimals) { text = Fmt("%.*f", decimals, v); fresh = true; }

void NumberField::update() {
    for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
        const bool digit = c >= '0' && c <= '9';
        const bool dot = c == '.';
        const bool minus = c == '-';
        if (!digit && !dot && !minus) continue;
        if (fresh) { text.clear(); fresh = false; }
        if (dot && text.find('.') != std::string::npos) continue;
        if (minus && !text.empty()) continue;
        if (text.size() < 9) text.push_back(char(c));
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty()) { text.pop_back(); fresh = false; }
}

double NumberField::value() const {
    try { return text.empty() ? 0.0 : std::stod(text); } catch (...) { return 0.0; }
}

bool NumberBox::draw(Rectangle r, double current, int decimals, bool enabled) {
    const bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    if (!enabled) active = false;
    if (enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (hover && !active) { active = true; field.set(current, decimals); }
        else if (!hover) active = false;
    }
    bool committed = false;
    if (active) {
        field.update();
        if (EnterPressed()) { active = false; committed = true; }
    } else {
        field.set(current, decimals);
    }
    DrawRectangleRounded(r, 0.2f, 6, active ? theme::kButtonHover : theme::kButton);
    DrawRectangleRoundedLines(r, 0.2f, 6, active ? theme::kAccent : theme::kButtonBorder);
    const bool caret = active && std::fmod(GetTime(), 1.0) < 0.5;
    const std::string shown = field.text + (caret ? "_" : "");
    DrawText(shown.c_str(), int(r.x + 10), int(r.y + (r.height - 18) / 2), 18, active ? theme::kAccent : theme::kInk);
    return committed;
}

bool EnterPressed() { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER); }

void SectionLabel(float x, float y, const char* text) { DrawText(text, int(x), int(y), 15, theme::kAccent); }

void DrawPanel(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, Fade(theme::kPanel, 0.86f));
    DrawRectangleLines(x, y, w, h, Fade(theme::kButtonBorder, 0.9f));
}

void DrawMenuBackground() {
    const int width = GetScreenWidth(), height = GetScreenHeight();
    DrawRectangleGradientV(0, 0, width, height, Color{13, 22, 37, 255}, Color{5, 9, 17, 255});
    DrawCircleGradient(width / 2, height / 2, float(height) * 0.55f, Fade(Color{34, 85, 105, 255}, 0.25f),
                       Fade(Color{5, 9, 17, 255}, 0.0f));
    DrawCircleLines(width / 2, height / 2, float(height) * 0.32f, Fade(theme::kAccent, 0.12f));
    DrawCircleLines(width / 2, height / 2, float(height) * 0.40f, Fade(theme::kAccent, 0.08f));
}

void DrawCenteredText(const char* text, int fontSize, float y, Color c) {
    DrawText(text, GetScreenWidth() / 2 - MeasureText(text, fontSize) / 2, int(y), fontSize, c);
}

int DrawLinesAt(const std::vector<std::string>& lines, int x, int y, int size, Color c) {
    for (const auto& l : lines) { DrawText(l.c_str(), x, y, size, c); y += size + 8; }
    return y;
}

} // namespace tess
