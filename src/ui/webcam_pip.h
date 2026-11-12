// Picture-in-picture: the webcam diagnostics image as a small translucent overlay in a scene.
#pragma once
#include "ui/ui_layer.h"

namespace tess {

class WebcamPip : public UiLayer {
public:
    void draw(AppState& state, const UiFrame& frame) override;
    void unload();   // frees the texture; call before CloseWindow()

private:
    Texture2D tex_{};
    std::uint64_t seq_ = 0;
};

} // namespace tess
