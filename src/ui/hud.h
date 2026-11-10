// The status overlay in the top-left corner (H toggles it) and the toast at the bottom.
#pragma once
#include "ui/ui_layer.h"

namespace tess {

class Hud : public UiLayer {
public:
    bool update(AppState&, const UiFrame&) override { return false; }   // display only
    void draw(AppState& state, const UiFrame& frame) override;
};

void DrawToast(const Toast& toast, int sw, int sh);

} // namespace tess
