#include "ui/calibration_ui.h"

#include "core/text.h"
#include "ui/file_dialog.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace tess {
namespace {

const char* kEyeNames[] = {"Both eyes (midpoint)", "Left eye", "Right eye"};
const char* kPointNames[] = {"1 top-left", "2 top-right", "3 bottom-right", "4 bottom-left", "5 webcam lens"};
constexpr float kPanelH = 150;   // instruction panel at the top of the photo page

bool LoadPhoto(const std::string& path, CalibrationUi::PhotoState& ps, std::string& err) {
    cv::Mat img;
    try { img = cv::imread(path, cv::IMREAD_COLOR); } catch (const cv::Exception&) { img.release(); }
    if (img.empty()) { err = "Could not read that image. Use JPG or PNG (iPhone HEIC photos must be converted)."; return false; }
    const double sc = std::min(1.0, 2048.0 / std::max(img.cols, img.rows));
    if (sc < 1.0) cv::resize(img, img, cv::Size(), sc, sc, cv::INTER_AREA);
    cv::Mat rgba;
    cv::cvtColor(img, rgba, cv::COLOR_BGR2RGBA);
    Image im{rgba.data, rgba.cols, rgba.rows, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    if (ps.loaded) UnloadTexture(ps.tex);
    ps.tex = LoadTextureFromImage(im);
    GenTextureMipmaps(&ps.tex);
    SetTextureFilter(ps.tex, TEXTURE_FILTER_TRILINEAR);
    ps.loaded = ps.tex.id != 0;
    ps.pts.clear();
    ps.stage = 0;
    ps.name = std::filesystem::path(path).filename().string();
    if (!ps.loaded) err = "Could not upload the image to the GPU.";
    return ps.loaded;
}

// The 4 clicked corners define a homography from the photo to the physical screen plane (mm), which maps the
// clicked webcam lens to millimetres from the screen center.
CalibrationUi::PhotoResult ComputePhotoResult(const std::vector<Vector2>& p, double diagIn, double aspect) {
    CalibrationUi::PhotoResult r;
    r.aspect = aspect;
    const double diagMm = diagIn * 25.4;
    r.hMm = diagMm / std::sqrt(1.0 + aspect * aspect);
    r.wMm = r.hMm * aspect;
    const std::vector<cv::Point2f> src = {{p[0].x, p[0].y}, {p[1].x, p[1].y}, {p[2].x, p[2].y}, {p[3].x, p[3].y}};
    const std::vector<cv::Point2f> dst = {{float(-r.wMm / 2), float(r.hMm / 2)}, {float(r.wMm / 2), float(r.hMm / 2)},
                                          {float(r.wMm / 2), float(-r.hMm / 2)}, {float(-r.wMm / 2), float(-r.hMm / 2)}};
    const cv::Mat H = cv::getPerspectiveTransform(src, dst);
    std::vector<cv::Point2f> in{{p[4].x, p[4].y}}, out;
    cv::perspectiveTransform(in, out, H);
    r.camX = out[0].x; r.camY = out[0].y;
    if (!(p[0].x < p[1].x && p[3].x < p[2].x && p[0].y < p[3].y && p[1].y < p[2].y))
        r.warnings.push_back("The corners don't look like top-left, top-right, bottom-right, bottom-left order.");
    auto dist = [](Vector2 a, Vector2 b) { return std::hypot(a.x - b.x, a.y - b.y); };
    const double wpx = (dist(p[0], p[1]) + dist(p[3], p[2])) * 0.5, hpx = (dist(p[0], p[3]) + dist(p[1], p[2])) * 0.5;
    r.photoAspect = wpx / std::max(1.0, hpx);
    if (std::abs(r.photoAspect / aspect - 1.0) > 0.12)
        r.warnings.push_back(Fmt("Photo shape %.2f:1 vs screen %.2f:1. Retake more straight-on, or check the corners.",
                                 r.photoAspect, aspect));
    if (std::abs(r.camX) < r.wMm / 2 && std::abs(r.camY) < r.hMm / 2)
        r.warnings.push_back("The webcam point is inside the screen area. Check the clicks.");
    if (std::abs(r.camX) > r.wMm || std::abs(r.camY) > r.hMm)
        r.warnings.push_back("The webcam point is very far from the screen. Check the clicks.");
    return r;
}

// Where the photo is drawn: scaled to fit below the instruction panel.
struct PhotoLayout { float fit; Vector2 org; };
PhotoLayout LayoutPhoto(const CalibrationUi::PhotoState& photo, int sw, int sh) {
    const float pad = 20;
    const float fit = std::min((sw - 2 * pad) / float(photo.tex.width), (sh - kPanelH - pad) / float(photo.tex.height));
    return {fit, {(sw - photo.tex.width * fit) * 0.5f, kPanelH + (sh - kPanelH - pad - photo.tex.height * fit) * 0.5f}};
}

} // namespace

// ------------------------------------------------------------------ input ----
bool CalibrationUi::loadPhotoFile(AppState& st, const std::string& path) {
    std::string err;
    if (LoadPhoto(path, photo_, err)) return true;
    st.toast.show(err);
    return false;
}

void CalibrationUi::computePhoto(AppState& st, const UiFrame& f) {
    const double diag = field_.value();
    if (!(diag > 5 && diag < 200)) { st.toast.show("Enter the monitor diagonal in inches (5 to 200)", 4); return; }
    photoRes_ = ComputePhotoResult(photo_.pts, diag, f.geo.monAspect);
    photo_.stage = 2;
}

void CalibrationUi::commitPhoto(AppState& st) {
    Config& cfg = st.cfg;
    cfg.screenDiagIn = field_.value();
    cfg.screenWmm = photoRes_.wMm; cfg.screenHmm = photoRes_.hMm;
    cfg.camOffXmm = photoRes_.camX; cfg.camOffYmm = photoRes_.camY;
    cfg.photoDone = true;
    st.requestSave();
    st.paramsDirty = true;
    st.physicalAlignment = true;
    st.toast.show(Fmt("Saved: screen %.0f x %.0f mm, webcam at X %.0f, Y %.0f mm from center. Using true geometry.",
                      cfg.screenWmm, cfg.screenHmm, cfg.camOffXmm, cfg.camOffYmm));
    st.backToSettings();
}

bool CalibrationUi::update(AppState& st, const UiFrame& f) {
    if (st.page != Page::Calibration && st.page != Page::PenTipTest) return false;
    if (st.page == Page::PenTipTest) {
        if (IsKeyPressed(KEY_ESCAPE)) st.backToSettings();
        return true;
    }

    if (IsFileDropped()) {   // dropping a photo anywhere on this page loads it
        FilePathList dropped = LoadDroppedFiles();
        if (dropped.count > 0) loadPhotoFile(st, dropped.paths[0]);
        UnloadDroppedFiles(dropped);
    }
    if (photo_.loaded && photo_.stage == 1) {
        field_.update();
        if (EnterPressed()) computePhoto(st, f);
    } else if (photo_.loaded && photo_.stage == 2 && EnterPressed()) {
        commitPhoto(st);
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (!photo_.loaded || photo_.stage == 0) st.backToSettings();
        else if (photo_.stage == 1) { photo_.pts.pop_back(); photo_.stage = 0; }
        else { photo_.pts.clear(); photo_.stage = 0; }
    }
    return true;
}

// ------------------------------------------------------------------ draw -----
void CalibrationUi::draw(AppState& st, const UiFrame& f) {
    if (st.page == Page::Calibration) drawCalibration(st, f);
    else if (st.page == Page::PenTipTest) drawPenTip(st, f);
}

void CalibrationUi::drawCalibration(AppState& st, const UiFrame& f) {
    const int sw = f.geo.sw, sh = f.geo.sh;
    if (!photo_.loaded) {
        // White on purpose: this is what you photograph.
        ClearBackground(WHITE);
        DrawRectangleLinesEx({0, 0, float(sw), float(sh)}, 6, BLACK);
        DrawLinesAt({"SCREEN PHOTO: exact screen size and webcam position",
                     "",
                     "1. Make this window fullscreen (F11) so the whole screen is white.",
                     "2. Take a photo of the monitor from straight in front, about 1-2 m away.",
                     "   The whole screen and the webcam must be visible. 2x zoom on the phone helps.",
                     "3. Copy the photo (JPG or PNG) to this computer.",
                     "4. Load it with the button below, or drag and drop it onto this window."}, 40, 40, 22, BLACK);
        if (Button({40, 290, 260, 46}, "Browse for photo...", true, 18)) {
            const std::string path = OpenImageFileDialog(GetWindowHandle());
            if (!path.empty()) loadPhotoFile(st, path);
        }
        if (Button({316, 290, 160, 46}, "Back", true, 18)) st.backToSettings();
        return;
    }

    ClearBackground(Color{25, 25, 30, 255});
    const PhotoLayout lay = LayoutPhoto(photo_, sw, sh);
    DrawTextureEx(photo_.tex, lay.org, 0.0f, lay.fit, WHITE);
    auto toScreen = [&](Vector2 p) { return Vector2{lay.org.x + p.x * lay.fit, lay.org.y + p.y * lay.fit}; };
    for (size_t i = 0; i < photo_.pts.size(); ++i) {
        const Vector2 s = toScreen(photo_.pts[i]);
        if (i < 4 && i > 0) DrawLineEx(toScreen(photo_.pts[i - 1]), s, 2, YELLOW);
        if (i == 3) DrawLineEx(s, toScreen(photo_.pts[0]), 2, YELLOW);
        const Color c = i == 4 ? RED : YELLOW;
        DrawCircleLines(int(s.x), int(s.y), 7, c);
        DrawLine(int(s.x) - 12, int(s.y), int(s.x) + 12, int(s.y), c);
        DrawLine(int(s.x), int(s.y) - 12, int(s.x), int(s.y) + 12, c);
        DrawText(kPointNames[i], int(s.x) + 10, int(s.y) + 8, 18, c);
    }
    DrawPanel(0, 0, sw, int(kPanelH) - 10);
    const float by = kPanelH - 56;   // button row inside the panel

    if (photo_.stage == 0) {
        DrawLinesAt({Fmt("Click point %d of 5:  %s", int(photo_.pts.size()) + 1, kPointNames[std::min<size_t>(photo_.pts.size(), 4)]),
                     "Corners = the corners of the LIT picture area (not the bezel), as you face the screen.  Webcam = the center of the lens.",
                     "Photo: " + photo_.name + "   (drop or browse another photo to replace it)"}, 16, 12, 18, theme::kInk);
        if (Button({16, by, 110, 36}, "Undo click", !photo_.pts.empty(), 16) && !photo_.pts.empty()) photo_.pts.pop_back();
        if (Button({136, by, 150, 36}, "Browse photo...", true, 16)) {
            const std::string path = OpenImageFileDialog(GetWindowHandle());
            if (!path.empty()) loadPhotoFile(st, path);
        }
        if (Button({296, by, 110, 36}, "Back", true, 16)) st.backToSettings();
        // Clicking on the photo adds a point; a magnifier follows the mouse.
        const Vector2 m = GetMousePosition();
        const Vector2 ip{(m.x - lay.org.x) / lay.fit, (m.y - lay.org.y) / lay.fit};
        const bool inside = m.y > kPanelH && ip.x >= 0 && ip.y >= 0 && ip.x < photo_.tex.width && ip.y < photo_.tex.height;
        if (inside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && photo_.pts.size() < 5) photo_.pts.push_back(ip);
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !photo_.pts.empty()) photo_.pts.pop_back();
        if (photo_.pts.size() == 5) { photo_.stage = 1; field_.set(st.cfg.screenDiagIn > 0 ? st.cfg.screenDiagIn : 27.0, 1); }
        if (inside) {
            const float box = 220, zoom = 5.0f;
            const float srcHalf = box / (lay.fit * zoom) * 0.5f;
            Vector2 at{m.x + 24, m.y + 24};
            if (at.x + box > sw) at.x = m.x - 24 - box;
            if (at.y + box > sh) at.y = m.y - 24 - box;
            DrawTexturePro(photo_.tex, {ip.x - srcHalf, ip.y - srcHalf, srcHalf * 2, srcHalf * 2},
                           {at.x, at.y, box, box}, {0, 0}, 0.0f, WHITE);
            DrawRectangleLinesEx({at.x, at.y, box, box}, 2, RAYWHITE);
            DrawLine(int(at.x + box / 2), int(at.y), int(at.x + box / 2), int(at.y + box), RED);
            DrawLine(int(at.x), int(at.y + box / 2), int(at.x + box), int(at.y + box / 2), RED);
        }
    } else if (photo_.stage == 1) {
        DrawLinesAt({"Monitor diagonal size in inches (the advertised size, e.g. 27). Type it, then Compute.",
                     "> " + field_.text + "_",
                     Fmt("Aspect ratio comes from this monitor's resolution: %dx%d (%.3f:1).", int(f.geo.monWpx), int(f.geo.monHpx), f.geo.monAspect)},
                    16, 12, 18, theme::kInk);
        if (Button({16, by, 130, 36}, "Compute", true, 16)) computePhoto(st, f);
        if (Button({156, by, 170, 36}, "Redo last click", true, 16)) { photo_.pts.pop_back(); photo_.stage = 0; }
        if (Button({336, by, 110, 36}, "Back", true, 16)) st.backToSettings();
    } else {
        std::vector<std::string> lines = {
            Fmt("Screen: %.0f x %.0f mm.  Webcam lens: X %+.0f mm, Y %+.0f mm from the screen center (%.0f mm above the top edge).",
                photoRes_.wMm, photoRes_.hMm, photoRes_.camX, photoRes_.camY, photoRes_.camY - photoRes_.hMm / 2)};
        for (const auto& w : photoRes_.warnings) lines.push_back("Warning: " + w);
        if (lines.size() == 1) lines.push_back("Looks good. Save to use the true geometry.");
        DrawLinesAt(lines, 16, 12, 18, photoRes_.warnings.empty() ? GREEN : theme::kWarn);
        if (Button({16, by, 130, 36}, "Save", true, 16)) commitPhoto(st);
        if (Button({156, by, 170, 36}, "Redo the clicks", true, 16)) { photo_.pts.clear(); photo_.stage = 0; }
        if (Button({336, by, 110, 36}, "Back", true, 16)) st.backToSettings();
    }
}

void CalibrationUi::drawPenTip(AppState& st, const UiFrame& f) {
    const float panelX = 20;
    const float panelY = 18;
    const float panelW = std::min(720.0f, float(f.geo.sw) - 40.0f);
    const float panelH = 346;
    const float contentX = panelX + 24;
    const float contentW = panelW - 48;

    DrawRectangleRounded({panelX, panelY, panelW, panelH}, 0.04f, 10, Fade(theme::kPanel, 0.94f));
    DrawRectangleRoundedLines({panelX, panelY, panelW, panelH}, 0.04f, 10, theme::kButtonBorder);
    DrawText("PEN-TIP ALIGNMENT TEST", int(contentX), int(panelY + 16), 22, theme::kInk);
    DrawText("Hold a real pen at the indicated distance and move your head side to side.",
             int(contentX), int(panelY + 48), 14, theme::kMuted);
    DrawText("Adjust webcam FOV until the red marker stays aligned with the pen tip.",
             int(contentX), int(panelY + 68), 14, theme::kMuted);

    SectionLabel(contentX, panelY + 94, "TEST CONTROLS");
    Slider(contentX, panelY + 114, contentW, "Pen-tip distance from screen", st.penTipMm, 50, 1000, "%.0f mm");
    double horizontalFov = st.cfg.hfovDeg();
    if (Slider(contentX, panelY + 162, contentW, "Webcam horizontal FOV", horizontalFov, 20, 160, "%.1f deg"))
        st.setFov(horizontalFov, false);

    SectionLabel(contentX, panelY + 210, "TRACKED EYE");
    const float gap = 8;
    const float eyeW = (contentW - 2 * gap) / 3;
    const float eyeY = panelY + 230;
    for (int i = 0; i < 3; ++i) {
        if (Toggle({contentX + i * (eyeW + gap), eyeY, eyeW, 34}, kEyeNames[i], st.eyeSelect == i, true, 15))
            st.eyeSelect = i;
    }

    const std::string tracking = f.snap.tracking
        ? Fmt("Tracking face | raw eye distance %.0f mm", f.snap.rawZ)
        : "Not tracking a face";
    DrawText(tracking.c_str(), int(contentX), int(panelY + 276), 14, f.snap.tracking ? theme::kAccent : theme::kWarn);
    DrawText(Fmt("%s | prediction %.0f ms", f.snap.status.c_str(), f.predictS * 1000.0).c_str(),
             int(contentX), int(panelY + 296), 13, theme::kMuted);

    if (Button({contentX, panelY + 318, 120, 34}, "Back to settings", true, 15)) st.backToSettings();
    DrawText("ESC also returns to settings", int(contentX + 136), int(panelY + 328), 13, theme::kMuted);
}


void CalibrationUi::unload() {
    if (photo_.loaded) { UnloadTexture(photo_.tex); photo_.loaded = false; }
}

} // namespace tess
