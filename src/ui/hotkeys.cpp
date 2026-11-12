#include "ui/hotkeys.h"

#include <raylib.h>

namespace tess {

void HandleGlobalHotkeys(AppState& st) {
    if (IsKeyPressed(KEY_F11)) st.fullscreen = !st.fullscreen;
}

void HandleSceneHotkeys(AppState& st) {
    if (IsKeyPressed(KEY_ESCAPE)) st.page = Page::StartMenu;
    if (IsKeyPressed(KEY_R)) st.recenter = true;
}

} // namespace tess
