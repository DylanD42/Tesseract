#pragma once

namespace tesseract::ui {
enum class MenuAction {
    None,
    SelectScene,
    SelectFloatingShapes,
    SelectAnimatedSquare,
    OpenSettings,
    Back,
    Exit
};

MenuAction drawStartMenu(bool selectingScene,bool interactive);
}
