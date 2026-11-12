// Full-window calibration page, opened from the Settings panel: load a photo of the setup (Browse or drag and
// drop), click the 4 screen corners and the webcam lens, type the diagonal in inches -> exact screen size and
// webcam position.
#pragma once
#include "ui/ui_layer.h"
#include "ui/ui_widgets.h"

#include <string>
#include <vector>

namespace tess {

class CalibrationUi : public UiLayer {
public:
    bool update(AppState& state, const UiFrame& frame) override;
    void draw(AppState& state, const UiFrame& frame) override;
    void unload();   // frees the photo texture; call before CloseWindow()

    struct PhotoState {
        bool loaded = false;
        Texture2D tex{};
        std::vector<Vector2> pts;   // clicked points in photo pixels: 4 corners then the webcam lens
        int stage = 0;              // 0 clicking, 1 typing the diagonal, 2 confirming the result
        std::string name;
    };
    struct PhotoResult {
        double wMm = 0, hMm = 0, camX = 0, camY = 0, aspect = 0, photoAspect = 0;
        std::vector<std::string> warnings;
    };

private:
    bool loadPhotoFile(AppState& st, const std::string& path);
    void computePhoto(AppState& st, const UiFrame& f);
    void commitPhoto(AppState& st);
    void drawCalibration(AppState& st, const UiFrame& f);

    NumberField field_;
    PhotoState photo_;
    PhotoResult photoRes_;
};

} // namespace tess
