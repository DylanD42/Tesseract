// The few keyboard shortcuts. Everything else is done with the mouse through the menus and the settings panel.
#pragma once
#include "core/app_state.h"

namespace tess {

// Always active: F11 fullscreen.
void HandleGlobalHotkeys(AppState& state);

// Active on the Scene page when no UI layer owns the input: ESC back to the start menu, R recenter.
void HandleSceneHotkeys(AppState& state);

} // namespace tess
