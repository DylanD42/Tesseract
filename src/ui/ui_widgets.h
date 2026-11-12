// Immediate-mode widgets in the Tesseract look (dark navy panels, teal accent). Drawing a widget also handles
// its input and reports the result, so call them from UiLayer::draw().
#pragma once
#include <raylib.h>
#include <string>
#include <vector>

namespace tess {

namespace theme {
inline constexpr Color kInk{230, 236, 244, 255};
inline constexpr Color kMuted{151, 167, 187, 255};
inline constexpr Color kAccent{79, 205, 211, 255};
inline constexpr Color kPanel{17, 24, 36, 255};
inline constexpr Color kButton{34, 52, 72, 255};
inline constexpr Color kButtonHover{51, 82, 101, 255};
inline constexpr Color kButtonBorder{61, 79, 99, 255};
inline constexpr Color kOn{32, 104, 112, 255};
inline constexpr Color kTrack{43, 59, 79, 255};
inline constexpr Color kWarn{255, 176, 72, 255};
} // namespace theme

// Rounded button with a centered label. Returns true on click.
bool Button(Rectangle r, const char* label, bool enabled = true, int fontSize = 18);

// Button that shows an on/off state. Returns true on click (the caller flips the value).
bool Toggle(Rectangle r, const char* label, bool on, bool enabled = true, int fontSize = 16);

// Horizontal slider with a label on the left and the value on the right. Occupies about 40 px of height.
// Returns true on the frames where the value changed.
bool Slider(float x, float y, float w, const char* label, double& value, double low, double high, const char* fmt,
            bool enabled = true);

// Click-to-edit numeric box. Shows `current` while idle; returns true when the user pressed Enter
// (the typed value is then in field.value()).
struct NumberField {
    std::string text;
    bool fresh = true;
    void set(double v, int decimals);
    void update();                // call once per frame while the field has focus
    double value() const;
};
struct NumberBox {
    NumberField field;
    bool active = false;
    bool draw(Rectangle r, double current, int decimals, bool enabled = true);
};

bool EnterPressed();

void SectionLabel(float x, float y, const char* text);
void DrawPanel(int x, int y, int w, int h);                       // translucent dark panel with a light border
void DrawMenuBackground();                                        // full-window gradient used by the menus
void DrawCenteredText(const char* text, int fontSize, float y, Color c);
int DrawLinesAt(const std::vector<std::string>& lines, int x, int y, int size, Color c);   // returns next y

} // namespace tess
