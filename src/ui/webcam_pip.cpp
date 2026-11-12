#include "ui/webcam_pip.h"

#include "ui/ui_widgets.h"

namespace tess {

void WebcamPip::draw(AppState& st, const UiFrame& f) {
    if (st.page != Page::Scene || st.webcamView() != WebcamView::PictureInPicture) return;
    if (f.webcamRgba && !f.webcamRgba->empty() && f.webcamSeq != seq_) {
        const cv::Mat& img = *f.webcamRgba;
        if (!tex_.id || tex_.width != img.cols || tex_.height != img.rows) {
            unload();
            Image im{img.data, img.cols, img.rows, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            tex_ = LoadTextureFromImage(im);
            SetTextureFilter(tex_, TEXTURE_FILTER_BILINEAR);
        } else {
            UpdateTexture(tex_, img.data);
        }
        seq_ = f.webcamSeq;
    }
    if (!tex_.id) return;
    const float dw = f.geo.sw * 0.22f, dh = dw * float(tex_.height) / float(tex_.width);
    const Rectangle dst{float(f.geo.sw) - dw - 16, float(f.geo.sh) - dh - 16, dw, dh};
    DrawTexturePro(tex_, {0, 0, float(tex_.width), float(tex_.height)}, dst, {0, 0}, 0.0f, Fade(WHITE, 0.72f));
    DrawRectangleLinesEx(dst, 1, Fade(theme::kAccent, 0.6f));
}

void WebcamPip::unload() {
    if (tex_.id) { UnloadTexture(tex_); tex_ = Texture2D{}; }
}

} // namespace tess
