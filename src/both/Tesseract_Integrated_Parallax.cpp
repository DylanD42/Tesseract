// Tesseract: head-tracked "window" parallax
// YuNet (OpenCV FaceDetectorYN) face box -> dlib 68 landmarks -> solvePnP head pose -> raylib off-axis projection.
// Link with raylib, OpenCV (core, calib3d, videoio, highgui, imgproc, imgcodecs, objdetect, dnn) and dlib.
// Requires OpenCV >= 4.8 for the 2023mar YuNet model.
//
// Threads
//   grab thread     : always holds the newest 640x480 webcam frame (no stale buffered frames)
//   tracking thread : YuNet face box -> dlib landmarks on an upscaled face crop -> sub-pixel landmark stabilizer
//                     -> solvePnP (depth) + eye corners (direction) -> screen-space mm -> One-Euro filter
//   render thread   : off-axis frustum from the eye through the physical screen rectangle, scenes, setup UI
//
// Setup (press C). Saved to tesseract_calibration.yml in the working directory.
//   1. Screen photo        : drag a photo of the setup onto the window, click the 4 screen corners and the webcam lens,
//                            type the diagonal in inches -> exact screen size and webcam position.
//   2. Webcam field of view: typed in (diagonal from the spec sheet, or horizontal). Sets the distance scale.
// V opens the pen-tip test to check the setup.
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/objdetect.hpp>
#include <dlib/image_processing.h>
#include <dlib/opencv.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double Seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

// ---------------------------------------------------------------- config ----
constexpr int   kWinW = 1000, kWinH = 800;
constexpr int   kCalibrationFrames = 30;      // measurements averaged for the recentered mode's reference (~1 s)
constexpr float kScreenHeightUnits = 10.0f;   // world units spanned by the window height
constexpr float kScreenPlaneZ = 1.0f;         // the "glass" sits at this world Z
constexpr float kScreenCenterY = 1.0f;
constexpr float kNearPlane = 0.3f, kFarPlane = 5000.0f;

const char* kConfigFile = "tesseract_calibration.yml";

// Camera: 640x480 (fast and steady; higher resolutions cost more than they gained).
constexpr int kCamW = 640, kCamH = 480;
constexpr int kMaxOpenCvThreads = 4;           // leave the other cores for rendering and capture

// Defaults used until setup has been done.
constexpr double kDefaultScreenHeightMm = 300.0;  // only if the monitor doesn't report its size
constexpr double kWebcamAboveTopEdgeMm  = 10.0;   // assumed webcam position: centered, just above the top edge
constexpr double kDefaultHFovDeg        = 70.0;   // webcam horizontal field of view until one is entered
constexpr double kDefaultStrength       = 0.75;   // fraction of physical motion ([ ] adjusts, 1.0 = exact)

// Face detection.
constexpr int   kDetectWidth = 640;           // YuNet input width (frames wider than this are downscaled)
constexpr float kYuNetScoreThreshold = 0.7f;
constexpr float kYuNetNmsThreshold   = 0.3f;
constexpr int   kYuNetTopK           = 50;

// Latency compensation.
constexpr double kExtraLatencyS     = 0.045;
constexpr double kMaxExtrapolationS = 0.10;
constexpr double kPredictMinSpeed   = 40.0;   // mm/s: no prediction below this (prediction amplifies noise)
constexpr double kPredictFullSpeed  = 150.0;  // mm/s
constexpr double kTrackGapResetS    = 0.5;
constexpr float  kRenderSmoothRate  = 60.0f;

// One-Euro filter (mm, Hz). Depth is filtered much harder: depth jitter makes the scene "breathe".
constexpr double kXYMinCutoff = 0.6, kXYBeta = 0.015;
constexpr double kZMinCutoff  = 0.25, kZBeta = 0.004;
constexpr double kDCutoff     = 1.0;

// Landmark stabilizer (sub-pixel template matching, anchored to dlib).
constexpr double kRefineMinScore = 0.85;
constexpr float  kRefineAnchor   = 0.25f;

// dlib.
constexpr int   kUpscaleBelowPx = 260;    // run dlib on a 2x crop when the face box is narrower than this
constexpr float kBoxScale  = 0.90f;       // YuNet box -> dlib-style box
constexpr float kBoxShiftY = 0.00f;

const char* kYuNetModelAbs = R"(C:\Users\danie\Desktop\raylibteam\Tesseract\custom\face_detection_yunet_2023mar.onnx)";

// Generic face model in mm, OpenCV camera convention (x right, y down, z away from camera). Origin = nose tip.
const int kLandmarkIdx[] = {30, 33, 8, 36, 45, 39, 42, 48, 54};
const std::vector<cv::Point3d> kFaceModel = {
    {   0,   0,  0},   // 30 nose tip
    {   0,  16, 14},   // 33 base of nose
    {   0,  80, 25},   // 8  chin
    { -45, -35, 38},   // 36 outer eye corner (image left  = subject's RIGHT eye)
    {  45, -35, 38},   // 45 outer eye corner (image right = subject's LEFT eye)
    { -16, -35, 30},   // 39 inner eye corner (subject's right eye)
    {  16, -35, 30},   // 42 inner eye corner (subject's left eye)
    { -25,  35, 25},   // 48 mouth corner
    {  25,  35, 25}    // 54 mouth corner
};
const cv::Point3d kEyeCenterInFaceMm(0.0, -35.0, 44.0);   // only its depth is used

// ------------------------------------------------------------- filtering ----
struct OneEuro {
    double minCutoff = 1.0, beta = 0.0, dCutoff = kDCutoff;
    bool init = false; double xPrev = 0, dxPrev = 0;
    static double alpha(double cutoff, double dt) { return 1.0 / (1.0 + (1.0 / (2.0 * CV_PI * cutoff)) / dt); }
    double filter(double x, double dt) {
        if (!init) { init = true; xPrev = x; dxPrev = 0; return x; }
        const double dx = (x - xPrev) / dt;
        const double ad = alpha(dCutoff, dt);
        dxPrev = ad * dx + (1.0 - ad) * dxPrev;
        const double a = alpha(minCutoff + beta * std::abs(dxPrev), dt);
        xPrev = a * x + (1.0 - a) * xPrev;
        return xPrev;
    }
    void reset() { init = false; dxPrev = 0; }
};

// ------------------------------------------------------- persistent config --
// Horizontal FOV of the 640x480 image from a diagonal FOV (spec sheets usually give the diagonal).
double DiagonalToHorizontalFov(double diagDeg) {
    const double diagPx = std::hypot(double(kCamW), double(kCamH));
    return 2.0 * std::atan(std::tan(diagDeg * CV_PI / 360.0) * kCamW / diagPx) * 180.0 / CV_PI;
}

struct Config {
    double screenDiagIn = 0, screenWmm = 0, screenHmm = 0;          // from the photo step
    double camOffXmm = 0, camOffYmm = 0, camOffZmm = 0;             // webcam lens vs screen center (+Y up, +Z toward you)
    bool   photoDone = false;
    double fovDeg = 0;            // as entered; 0 = not entered (default horizontal FOV is used)
    bool   fovDiagonal = true;    // how fovDeg was entered
    double strength = kDefaultStrength;
    int    scene = 0;

    double hfovDeg() const {
        if (!(fovDeg > 10 && fovDeg < 170)) return kDefaultHFovDeg;
        return fovDiagonal ? DiagonalToHorizontalFov(fovDeg) : fovDeg;
    }

    bool load(const std::string& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return false;
        cv::FileStorage fs;
        try { if (!fs.open(path, cv::FileStorage::READ)) return false; } catch (const cv::Exception&) { return false; }
        auto rd = [&](const char* k, double& v) { cv::FileNode n = fs[k]; if (!n.empty() && (n.isReal() || n.isInt())) v = n.real(); };
        auto ri = [&](const char* k, int& v) { cv::FileNode n = fs[k]; if (!n.empty() && n.isInt()) v = int(n); };
        auto rb = [&](const char* k, bool& v) { cv::FileNode n = fs[k]; if (!n.empty() && n.isInt()) v = int(n) != 0; };
        rd("screen_diag_in", screenDiagIn); rd("screen_w_mm", screenWmm); rd("screen_h_mm", screenHmm);
        rd("cam_offset_x_mm", camOffXmm); rd("cam_offset_y_mm", camOffYmm); rd("cam_offset_z_mm", camOffZmm);
        rb("photo_done", photoDone);
        rd("fov_deg", fovDeg); rb("fov_diagonal", fovDiagonal);
        rd("strength", strength); ri("scene", scene);
        strength = std::clamp(strength, 0.1, 1.5);
        if (photoDone && !(screenHmm > 10)) photoDone = false;
        return true;
    }
    bool save(const std::string& path) const {
        cv::FileStorage fs;
        try { if (!fs.open(path, cv::FileStorage::WRITE)) return false; } catch (const cv::Exception&) { return false; }
        fs << "screen_diag_in" << screenDiagIn << "screen_w_mm" << screenWmm << "screen_h_mm" << screenHmm;
        fs << "cam_offset_x_mm" << camOffXmm << "cam_offset_y_mm" << camOffYmm << "cam_offset_z_mm" << camOffZmm;
        fs << "photo_done" << int(photoDone);
        fs << "fov_deg" << fovDeg << "fov_diagonal" << int(fovDiagonal);
        fs << "strength" << strength << "scene" << scene;
        return true;
    }
};

// Physical screen size: photo setup if done, else what the monitor reports (EDID), else a guess.
struct ScreenDims { double wMm = 0, hMm = 0; std::string source; };

ScreenDims ResolveScreen(const Config& cfg, int mon) {
    const double monW = std::max(1, GetMonitorWidth(mon)), monH = std::max(1, GetMonitorHeight(mon));
    const double aspect = monW / monH;
    if (cfg.photoDone && cfg.screenHmm > 10)
        return {cfg.screenWmm > 10 ? cfg.screenWmm : cfg.screenHmm * aspect, cfg.screenHmm, "photo"};
    const double pw = GetMonitorPhysicalWidth(mon), ph = GetMonitorPhysicalHeight(mon);
    if (pw > 100 && ph > 60 && ph < 2000 && std::abs((pw / ph) / aspect - 1.0) < 0.08)
        return {pw, ph, "monitor (EDID)"};
    return {kDefaultScreenHeightMm * aspect, kDefaultScreenHeightMm, "default guess"};
}

// ------------------------------------------------------- shared w/ render ---
struct Snapshot {
    bool valid = false;        // have a filtered eye position
    bool calibrated = false;   // recenter reference captured
    bool tracking = false;     // this frame produced a measurement
    int collected = 0, faceCount = 0;
    double reprojErr = 0;
    double faceScore = 0;
    double procMs = 0, workMs = 0, camFps = 0;
    double rawZ = 0;               // unfiltered eye distance (shown while entering the FOV)
    cv::Vec3d pos{0, 0, 0};        // filtered eye, SCREEN space mm (origin monitor center, +X right, +Y up, +Z toward you)
    cv::Vec3d vel{0, 0, 0};
    cv::Vec3d reference{0, 0, 0};  // recentered-mode reference (screen space)
    Clock::time_point stamp{};
    int camW = 0, camH = 0;
    std::string status = "Starting camera";
};

struct TrackerParams {
    double hfovDeg = kDefaultHFovDeg;
    cv::Vec3d offset{0, 0, 0};     // webcam lens position in screen space (mm)
};

struct Shared {
    std::mutex m;
    Snapshot snap;
    std::atomic<bool> quit{false};
    std::atomic<bool> recalibrate{false};
    std::atomic<int>  eyeSelect{0};          // 0 = midpoint, 1 = left eye, 2 = right eye (pen-tip test)
    std::mutex pm;
    TrackerParams params;
    std::atomic<int>  paramsVersion{0};
};

void PublishParams(Shared& sh, const Config& c, const ScreenDims& dims) {
    std::lock_guard<std::mutex> lk(sh.pm);
    sh.params.hfovDeg = c.hfovDeg();
    sh.params.offset = c.photoDone ? cv::Vec3d(c.camOffXmm, c.camOffYmm, c.camOffZmm)
                                   : cv::Vec3d(0, dims.hMm / 2 + kWebcamAboveTopEdgeMm, 0);
    ++sh.paramsVersion;
}

struct FrameSource {
    std::mutex m;
    std::condition_variable ready;
    cv::Mat frame;
    Clock::time_point stamp{};
    std::uint64_t seq = 0;
};

struct CamModel {
    cv::Mat K, dist;
    cv::Vec3d offset{0, 0, 0};
};

cv::Mat approximateIntrinsics(cv::Size size, double hfovDeg) {
    const double focal = size.width / (2.0 * std::tan(hfovDeg * CV_PI / 360.0));
    return (cv::Mat_<double>(3, 3) << focal, 0, size.width * 0.5, 0, focal, size.height * 0.5, 0, 0, 1);
}

CamModel BuildCamModel(const TrackerParams& p, cv::Size camSize) {
    CamModel m;
    m.K = approximateIntrinsics(camSize, p.hfovDeg);
    m.dist = cv::Mat::zeros(5, 1, CV_64F);
    m.offset = p.offset;
    return m;
}

// Debug drawing helper (coordinates are full-frame; s scales them onto the debug image).
struct Dbg {
    cv::Mat img;
    double s = 1.0;
    cv::Point P(cv::Point2d p) const { return cv::Point(cvRound(p.x * s), cvRound(p.y * s)); }
    void circle(cv::Point2d p, int r, cv::Scalar c, int th = 1) { cv::circle(img, P(p), r, c, th, cv::LINE_AA); }
    void rect(const cv::Rect2d& r, cv::Scalar c, int th = 1) {
        cv::rectangle(img, cv::Rect(cvRound(r.x * s), cvRound(r.y * s), cvRound(r.width * s), cvRound(r.height * s)), c, th);
    }
    cv::Mat scaledK(const cv::Mat& K) const {
        cv::Mat k = K.clone();
        k.at<double>(0, 0) *= s; k.at<double>(1, 1) *= s; k.at<double>(0, 2) *= s; k.at<double>(1, 2) *= s;
        return k;
    }
};

struct Tracker {
    Snapshot snap;
    int collected = 0;
    cv::Vec3d sum{0, 0, 0};
    OneEuro filt[3];
    cv::Mat rvec, tvec, small, dets, gray, prevGray;
    bool havePose = false, haveBox = false, havePrevPts = false, haveMeas = false;
    cv::Vec4d box{0, 0, 0, 0};
    std::vector<cv::Point2f> prevFused, prevDet;
    Clock::time_point lastMeas{};
    int eyeSel = 0;
    Tracker() {
        for (int i = 0; i < 2; ++i) { filt[i].minCutoff = kXYMinCutoff; filt[i].beta = kXYBeta; }
        filt[2].minCutoff = kZMinCutoff; filt[2].beta = kZBeta;
    }
    void resetFilters() { for (auto& f : filt) f.reset(); haveMeas = false; }
    void reset() {
        collected = 0; sum = cv::Vec3d(0, 0, 0);
        havePose = false; haveBox = false; havePrevPts = false;
        resetFilters();
        snap.valid = false; snap.calibrated = false; snap.collected = 0;
        snap.vel = cv::Vec3d(0, 0, 0);
        snap.reference = cv::Vec3d(0, 0, 0); snap.status = "Recentering";
    }
};

// ---------------------------------------------------------------- camera ----
bool openCamera(int index, cv::VideoCapture& camera, cv::Mat& frame) {
    for (int backend : {(int)cv::CAP_DSHOW, (int)cv::CAP_MSMF, (int)cv::CAP_ANY}) {
        // MJPG first: many webcams only reach 60 fps (lower latency) in MJPG.
        for (bool mjpg : {true, false}) {
            camera.release();
            if (!camera.open(index, backend)) break;
            if (mjpg) camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            camera.set(cv::CAP_PROP_FRAME_WIDTH, kCamW);
            camera.set(cv::CAP_PROP_FRAME_HEIGHT, kCamH);
            camera.set(cv::CAP_PROP_FPS, 60);
            camera.set(cv::CAP_PROP_BUFFERSIZE, 1);
            for (int attempt = 0; attempt < 20; ++attempt) {
                if (camera.read(frame) && !frame.empty()) {
                    std::cout << "Camera opened: " << frame.cols << "x" << frame.rows << " @ " << camera.get(cv::CAP_PROP_FPS)
                              << " fps (backend " << camera.getBackendName() << (mjpg ? ", MJPG" : "") << ")\n";
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    camera.release();
    return false;
}

// ------------------------------------------------------------ landmarks -----
// Sub-pixel re-location of a point between frames: correlate a patch from the previous frame around `guess`
// in the current frame and fit a parabola to the correlation peak.
bool RefinePoint(const cv::Mat& prevGray, const cv::Mat& gray, cv::Point2f prevPt, cv::Point2f guess,
                 int half, int search, cv::Point2f& out) {
    const int tw = 2 * half + 1, aw = 2 * (half + search) + 1;
    auto inside = [&](cv::Point2f p, int h) {
        return p.x > h + 1 && p.y > h + 1 && p.x < gray.cols - h - 2 && p.y < gray.rows - h - 2;
    };
    if (prevGray.size() != gray.size() || !inside(prevPt, half) || !inside(guess, half + search)) return false;
    cv::Mat tpl, area, res;
    cv::getRectSubPix(prevGray, cv::Size(tw, tw), prevPt, tpl, CV_32F);
    cv::getRectSubPix(gray, cv::Size(aw, aw), guess, area, CV_32F);
    cv::matchTemplate(area, tpl, res, cv::TM_CCOEFF_NORMED);
    double maxVal = 0; cv::Point m;
    cv::minMaxLoc(res, nullptr, &maxVal, nullptr, &m);
    if (!(maxVal >= kRefineMinScore) || m.x <= 0 || m.y <= 0 || m.x >= res.cols - 1 || m.y >= res.rows - 1) return false;
    auto peak = [](float a, float b, float c) {
        const float d = a - 2.0f * b + c;
        return std::fabs(d) < 1e-6f ? 0.0f : 0.5f * (a - c) / d;
    };
    const float dx = peak(res.at<float>(m.y, m.x - 1), res.at<float>(m.y, m.x), res.at<float>(m.y, m.x + 1));
    const float dy = peak(res.at<float>(m.y - 1, m.x), res.at<float>(m.y, m.x), res.at<float>(m.y + 1, m.x));
    out = guess + cv::Point2f(float(m.x - search) + dx, float(m.y - search) + dy);
    return true;
}

// Stabilize landmarks: each point is re-found by correlation from the previous frame (precise and smooth), then
// pulled a little toward the fresh dlib point so it cannot drift. Big disagreements (fast motion) use dlib directly,
// so there is no lag. Returns how many points were refined.
int StabilizePoints(Tracker& t, const std::vector<cv::Point2f>& det, std::vector<cv::Point2f>& fused,
                    int half, int search, float gate) {
    fused = det;
    int refined = 0;
    if (t.havePrevPts && t.prevFused.size() == det.size() && t.prevGray.size() == t.gray.size()) {
        for (size_t i = 0; i < det.size(); ++i) {
            const cv::Point2f guess = t.prevFused[i] + (det[i] - t.prevDet[i]);
            cv::Point2f ref;
            if (RefinePoint(t.prevGray, t.gray, t.prevFused[i], guess, half, search, ref) && cv::norm(ref - det[i]) < gate) {
                fused[i] = ref + kRefineAnchor * (det[i] - ref);
                ++refined;
            }
        }
    }
    t.prevFused = fused; t.prevDet = det;
    t.havePrevPts = true;
    return refined;
}

// -------------------------------------------------------------- tracking ----
bool UpdateTracking(const cv::Mat& frame, Dbg& dbg, cv::Ptr<cv::FaceDetectorYN>& yunet,
                    const dlib::shape_predictor& predictor, const CamModel& cam, Clock::time_point stamp, Tracker& t) {
    Snapshot& s = t.snap;
    s.tracking = false;

    // ---- YuNet face box ----
    const double scale = std::min(1.0, double(kDetectWidth) / frame.cols);
    const cv::Mat* in = &frame;
    if (scale < 1.0) { cv::resize(frame, t.small, cv::Size(), scale, scale, cv::INTER_AREA); in = &t.small; }
    yunet->setInputSize(in->size());
    t.dets.release();
    yunet->detect(*in, t.dets);   // Nx15: x y w h, 5 landmarks (x,y), score
    s.faceCount = t.dets.empty() ? 0 : t.dets.rows;
    if (t.dets.empty()) {
        s.status = "No face detected"; s.faceScore = 0;
        t.havePose = false; t.haveBox = false; t.havePrevPts = false;
        return false;
    }
    int best = 0;
    for (int r = 1; r < t.dets.rows; ++r)
        if (t.dets.at<float>(r, 2) * t.dets.at<float>(r, 3) > t.dets.at<float>(best, 2) * t.dets.at<float>(best, 3)) best = r;
    const float* d = t.dets.ptr<float>(best);
    s.faceScore = d[14];
    const double bx = d[0] / scale, by = d[1] / scale, bw = d[2] / scale, bh = d[3] / scale;
    dbg.rect(cv::Rect2d(bx, by, bw, bh), cv::Scalar(0, 255, 0), 1);

    // dlib's output depends on the input box, so box jitter becomes landmark jitter: smooth small box changes.
    cv::Vec4d nb(bx + bw * 0.5, by + bh * 0.5, bw, bh);
    if (t.haveBox) {
        const double move = cv::norm(nb - t.box) / std::max(1.0, bw);
        const double a = std::clamp(move / 0.08, 0.35, 1.0);
        nb = t.box + a * (nb - t.box);
    }
    t.box = nb; t.haveBox = true;

    const double hw = nb[2] * 0.5 * kBoxScale, hh = nb[3] * 0.5 * kBoxScale;
    const double cyBox = nb[1] + nb[3] * kBoxShiftY;
    const int L = std::max(0, int(std::lround(nb[0] - hw)));
    const int T = std::max(0, int(std::lround(cyBox - hh)));
    const int R = std::min(frame.cols - 1, int(std::lround(nb[0] + hw)));
    const int B = std::min(frame.rows - 1, int(std::lround(cyBox + hh)));
    if (R - L < 20 || B - T < 20) { s.status = "Face box too small"; t.havePose = false; return false; }

    // ---- dlib 68 landmarks, on a 2x upscaled crop when the face is small (halves dlib's whole-pixel rounding) ----
    const double up = (R - L) < kUpscaleBelowPx ? 2.0 : 1.0;
    const int mx = (R - L) / 4, my = (B - T) / 4;
    const cv::Rect roi(cv::Point(std::max(0, L - mx), std::max(0, T - my)),
                       cv::Point(std::min(frame.cols, R + mx), std::min(frame.rows, B + my)));
    cv::Mat crop;
    cv::resize(frame(roi), crop, cv::Size(), up, up, cv::INTER_LINEAR);
    dlib::cv_image<dlib::bgr_pixel> cropImg(crop);
    const dlib::rectangle faceUp(std::lround((L - roi.x) * up), std::lround((T - roi.y) * up),
                                 std::lround((R - roi.x) * up), std::lround((B - roi.y) * up));
    const auto lm = predictor(cropImg, faceUp);
    if (lm.num_parts() != 68) { s.status = "Invalid landmark count"; return false; }
    auto toFrame = [&](const dlib::point& p) {
        return cv::Point2f(float((p.x() + 0.5) / up - 0.5 + roi.x), float((p.y() + 0.5) / up - 0.5 + roi.y));
    };
    for (unsigned long i = 0; i < lm.num_parts(); ++i) dbg.circle(toFrame(lm.part(i)), 1, cv::Scalar(0, 200, 255), cv::FILLED);

    std::vector<cv::Point2f> det;
    for (int i : kLandmarkIdx) det.push_back(toFrame(lm.part(i)));
    const double eyeDistPx = std::max(1.0, double(cv::norm(det[3] - det[4])));

    // ---- stabilizer ----
    cv::cvtColor(frame, t.gray, cv::COLOR_BGR2GRAY);
    std::vector<cv::Point2f> fused;
    const int half = std::clamp(int(std::lround(0.08 * eyeDistPx)), 7, 21);
    const int search = std::clamp(int(std::lround(0.05 * eyeDistPx)), 4, 12);
    const int refined = StabilizePoints(t, det, fused, half, search, float(std::max(1.5, 0.035 * eyeDistPx)));
    std::swap(t.prevGray, t.gray);
    for (const auto& p : fused) dbg.circle(p, 3, cv::Scalar(255, 255, 0), 1);
    std::vector<cv::Point2d> img(fused.begin(), fused.end());

    // ---- head pose (for depth) ----
    const cv::Mat& K = cam.K;
    const double f = K.at<double>(0, 0), cx = K.at<double>(0, 2), cy = K.at<double>(1, 2);
    if (!t.havePose) {   // seed the solver with a sane pose so it can't flip behind the camera
        const double z0 = f * 90.0 / eyeDistPx;
        t.rvec = cv::Mat::zeros(3, 1, CV_64F);
        t.tvec = (cv::Mat_<double>(3, 1) << (img[0].x - cx) / f * z0, (img[0].y - cy) / f * z0, z0);
    }
    const bool ok = cv::solvePnP(kFaceModel, img, K, cam.dist, t.rvec, t.tvec, true, cv::SOLVEPNP_ITERATIVE);
    const double tz = ok ? t.tvec.at<double>(2) : -1.0;
    if (!ok || !std::isfinite(tz) || tz < 150 || tz > 3000) {
        s.status = "solvePnP failed / bad depth"; t.havePose = false; return false;
    }
    std::vector<cv::Point2d> reproj;
    cv::projectPoints(kFaceModel, t.rvec, t.tvec, K, cam.dist, reproj);
    double err = 0;
    for (size_t i = 0; i < img.size(); ++i) {
        err += cv::norm(img[i] - reproj[i]);
        dbg.circle(reproj[i], 3, cv::Scalar(0, 0, 255), 1);
    }
    err /= img.size();
    s.reprojErr = err;
    cv::drawFrameAxes(dbg.img, dbg.scaledK(K), cam.dist, t.rvec, t.tvec, 50.0f);
    if (!std::isfinite(err) || err > 0.30 * eyeDistPx) { s.status = "Poor PnP fit"; t.havePose = false; return false; }
    t.havePose = true;

    // ---- eye position: depth from PnP, direction from the eye corners (they don't move when you blink) ----
    cv::Mat Rm; cv::Rodrigues(t.rvec, Rm);
    cv::Mat eyeLocal = (cv::Mat_<double>(3, 1) << kEyeCenterInFaceMm.x, kEyeCenterInFaceMm.y, kEyeCenterInFaceMm.z);
    cv::Mat e = Rm * eyeLocal + t.tvec;
    const double Z = e.at<double>(2);
    const cv::Point2d rightEye = (img[3] + img[5]) * 0.5, leftEye = (img[4] + img[6]) * 0.5;
    const cv::Point2d eyePx = t.eyeSel == 1 ? leftEye : t.eyeSel == 2 ? rightEye : (leftEye + rightEye) * 0.5;
    dbg.circle(eyePx, 4, cv::Scalar(0, 0, 255), cv::FILLED);
    const cv::Vec3d camEye((eyePx.x - cx) / f * Z, (eyePx.y - cy) / f * Z, Z);
    if (!(std::isfinite(camEye[0]) && std::isfinite(camEye[1]) && std::isfinite(camEye[2]))) {
        s.status = "Invalid eye position"; t.havePose = false; return false;
    }
    s.rawZ = Z;

    // Webcam space (x image-right, y image-down, z toward you) -> screen space (+X your right, +Y up, +Z toward you).
    // The webcam is assumed to face straight out of the screen.
    const cv::Vec3d scr(-camEye[0] + cam.offset[0], -camEye[1] + cam.offset[1], camEye[2] + cam.offset[2]);

    // ---- filtering ----
    if (t.haveMeas && Seconds(stamp - t.lastMeas) > kTrackGapResetS) t.resetFilters();
    const double dt = t.haveMeas ? std::clamp(Seconds(stamp - t.lastMeas), 0.001, kTrackGapResetS) : 1.0 / 30.0;
    t.lastMeas = stamp; t.haveMeas = true;
    for (int i = 0; i < 3; ++i) { s.pos[i] = t.filt[i].filter(scr[i], dt); s.vel[i] = t.filt[i].dxPrev; }
    s.stamp = stamp;
    s.valid = true;

    if (t.collected < kCalibrationFrames) {
        t.sum += s.pos;
        ++t.collected;
        s.collected = t.collected;
        if (t.collected == kCalibrationFrames) { s.reference = t.sum / double(kCalibrationFrames); s.calibrated = true; }
    }
    s.tracking = true;
    s.status = !s.calibrated ? "Hold still: centering"
             : cv::format("%s | stabilized %d/9", err > 0.10 * eyeDistPx ? "Tracking (low confidence)" : "Tracking OK", refined);
    return true;
}

// ------------------------------------------------------------- threads ------
void grabWorker(cv::VideoCapture& capture, FrameSource& src, const std::atomic<bool>& quit) {
    while (!quit) {
        cv::Mat f;   // fresh buffer every frame, so the tracker's copy is never overwritten (no clone needed)
        if (capture.read(f) && !f.empty()) {
            const auto now = Clock::now();
            {
                std::lock_guard<std::mutex> lk(src.m);
                src.frame = f;
                src.stamp = now;
                ++src.seq;
            }
            src.ready.notify_one();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    src.ready.notify_all();
}

void trackingWorker(FrameSource& src, std::string yunetModel, const dlib::shape_predictor& predictor, Shared& shared) {
    const char* kWin = "Tesseract - Webcam Diagnostics";
    cv::namedWindow(kWin, cv::WINDOW_NORMAL);
    cv::resizeWindow(kWin, 760, 570);

    cv::Ptr<cv::FaceDetectorYN> yunet;
    try {
        yunet = cv::FaceDetectorYN::create(yunetModel, "", cv::Size(kCamW, kCamH), kYuNetScoreThreshold, kYuNetNmsThreshold,
                                           kYuNetTopK);
    } catch (const cv::Exception& e) {
        std::cerr << "YuNet load failed: " << e.what() << "\n";
        shared.quit = true;
        return;
    }

    Tracker t;
    CamModel cam;
    TrackerParams params;
    int paramsSeen = -1;
    cv::Size camSize;
    cv::Mat frame;
    Clock::time_point stamp{}, lastStamp{};
    std::uint64_t lastSeq = 0;
    double workEma = 0, intervalEma = 0;
    Dbg dbg;

    while (!shared.quit) {
        if (shared.recalibrate.exchange(false)) t.reset();
        bool paramsChanged = false;
        if (shared.paramsVersion.load() != paramsSeen) {
            { std::lock_guard<std::mutex> lk(shared.pm); params = shared.params; paramsSeen = shared.paramsVersion.load(); }
            paramsChanged = true;
        }
        const int eyeSel = shared.eyeSelect.load();
        if (eyeSel != t.eyeSel) { t.eyeSel = eyeSel; t.resetFilters(); }

        bool got = false;
        {
            std::unique_lock<std::mutex> lk(src.m);
            src.ready.wait_for(lk, std::chrono::milliseconds(200),
                               [&] { return src.seq != lastSeq || shared.quit.load(); });
            if (src.seq != lastSeq) { frame = src.frame; stamp = src.stamp; lastSeq = src.seq; got = true; }
        }
        if (shared.quit) break;

        if (got && !frame.empty()) {
            const auto workStart = Clock::now();
            if (frame.size() != camSize || paramsChanged) {
                if (frame.size() != camSize) t.reset();
                camSize = frame.size();
                cam = BuildCamModel(params, camSize);
                dbg.s = std::min(1.0, 960.0 / camSize.width);
                t.havePose = false;
                t.resetFilters();
            }
            if (dbg.s < 1.0) cv::resize(frame, dbg.img, cv::Size(), dbg.s, dbg.s, cv::INTER_AREA);
            else frame.copyTo(dbg.img);
            try {
                UpdateTracking(frame, dbg, yunet, predictor, cam, stamp, t);
            } catch (const cv::Exception& e) {
                t.snap.status = std::string("Tracking error: ") + e.what();
                t.havePose = false;
            }

            const double workMs = Seconds(Clock::now() - workStart) * 1000.0;
            workEma = workEma <= 0 ? workMs : 0.92 * workEma + 0.08 * workMs;
            if (lastStamp != Clock::time_point{}) {
                const double iv = Seconds(stamp - lastStamp) * 1000.0;
                if (iv > 1 && iv < 500) intervalEma = intervalEma <= 0 ? iv : 0.95 * intervalEma + 0.05 * iv;
            }
            lastStamp = stamp;

            Snapshot& s = t.snap;
            s.procMs = Seconds(Clock::now() - stamp) * 1000.0;
            s.workMs = workEma;
            s.camFps = intervalEma > 0 ? 1000.0 / intervalEma : 0.0;
            s.camW = camSize.width; s.camH = camSize.height;
            { std::lock_guard<std::mutex> lk(shared.m); shared.snap = s; }

            cv::flip(dbg.img, dbg.img, 1);   // mirrored for readability only
            auto put = [&](const std::string& msg, int y, cv::Scalar c = cv::Scalar(255, 255, 255)) {
                cv::putText(dbg.img, msg, cv::Point(12, y), cv::FONT_HERSHEY_SIMPLEX, 0.52, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
                cv::putText(dbg.img, msg, cv::Point(12, y), cv::FONT_HERSHEY_SIMPLEX, 0.52, c, 1, cv::LINE_AA);
            };
            put("Faces: " + std::to_string(s.faceCount) + " | " + s.status, 24,
                s.tracking ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 140, 255));
            put(cv::format("%dx%d @ %.0f fps | YuNet %.2f | PnP reproj %.2f px | work %.1f ms", camSize.width, camSize.height,
                           s.camFps, s.faceScore, s.reprojErr, workEma), 48);
            if (s.valid) put(cv::format("Eye mm (screen): X %.0f  Y %.0f  Z %.0f", s.pos[0], s.pos[1], s.pos[2]), 72);
            put(cv::format("Webcam FOV %.1f deg horizontal", params.hfovDeg), 96);
            put("R = recenter | Q/Esc = exit", 120);
            cv::imshow(kWin, dbg.img);
        } else {
            t.snap.tracking = false;
            t.snap.status = "Waiting for camera frames";
            std::lock_guard<std::mutex> lk(shared.m);
            shared.snap = t.snap;
        }

        const int key = cv::waitKey(1);
        if (key == 'r' || key == 'R') t.reset();
        if (key == 'q' || key == 'Q' || key == 27) shared.quit = true;
    }
    cv::destroyWindow(kWin);
}

// ------------------------------------------------------------ fullscreen ----
struct WindowedState { int x = 100, y = 100, w = kWinW, h = kWinH; };

void SetBorderlessFullscreen(bool on, WindowedState& saved) {
    if (on) {
        const Vector2 p = GetWindowPosition();
        saved = { (int)p.x, (int)p.y, GetScreenWidth(), GetScreenHeight() };
        const int mon = GetCurrentMonitor();
        const Vector2 mp = GetMonitorPosition(mon);
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowPosition((int)mp.x, (int)mp.y);
        SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
        HideCursor();
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowSize(saved.w, saved.h);
        SetWindowPosition(saved.x, saved.y);
        ShowCursor();
    }
}

// Asymmetric frustum from the eye through the physical screen rectangle.
void ApplyOffAxisProjection(Vector3 eye, float screenW, float screenH, float screenDist) {
    const float n = kNearPlane;
    const float left   = n * (-screenW * 0.5f - eye.x) / screenDist;
    const float right  = n * ( screenW * 0.5f - eye.x) / screenDist;
    const float bottom = n * (-screenH * 0.5f - (eye.y - kScreenCenterY)) / screenDist;
    const float top    = n * ( screenH * 0.5f - (eye.y - kScreenCenterY)) / screenDist;
    rlSetMatrixProjection(MatrixFrustum(left, right, bottom, top, n, kFarPlane));
}

// ------------------------------------------------------------- rendering ----
// Lighting without mesh normals (normals rebuilt from screen-space derivatives), optional texture, distance fog.
const char* kSceneVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 fragPos;
out vec2 fragTexCoord;
out vec4 fragColor;
void main() {
    fragPos = vertexPosition;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kSceneFS = R"(#version 330
in vec3 fragPos;
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec3 viewPos;
uniform vec3 lightDir;   // direction the light travels
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform float fogMax;
uniform float lit;       // 1 = shaded surfaces, 0 = lines / glow / shadows (fog only)
uniform float shine;     // specular amount: 1 = glossy objects, 0 = matte ground, rugs, grass
out vec4 finalColor;
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 col = fragColor.rgb * texel.rgb;
    vec3 toEye = viewPos - fragPos;
    if (lit > 0.5) {
        vec3 n = cross(dFdx(fragPos), dFdy(fragPos));
        float len = length(n);
        if (len > 1e-12) {
            n /= len;
            vec3 V = normalize(toEye);
            if (dot(n, V) < 0.0) n = -n;
            vec3 L = normalize(-lightDir);
            float diff = max(dot(n, L), 0.0);
            float hemi = 0.5 + 0.5 * n.y;
            vec3 H = normalize(L + V);
            float spec = pow(max(dot(n, H), 0.0), 40.0) * 0.25 * shine;
            col = col * (0.28 + 0.20 * hemi + 0.62 * diff) + vec3(spec);
        }
    }
    float fog = clamp((length(toEye) - fogNear) / max(fogFar - fogNear, 1e-3), 0.0, 1.0);
    col = mix(col, fogColor, fog * fogMax);
    finalColor = vec4(col, fragColor.a * texel.a);
}
)";

struct SceneShader {
    Shader sh{};
    bool ok = false;
    int viewPos = -1, lightDir = -1, fogColor = -1, fogNear = -1, fogFar = -1, fogMax = -1, lit = -1, shine = -1;
    void load() {
        sh = LoadShaderFromMemory(kSceneVS, kSceneFS);
        ok = IsShaderValid(sh) && sh.id != rlGetShaderIdDefault();
        if (!ok) { std::cerr << "Scene shader failed; falling back to unlit rendering\n"; return; }
        viewPos  = GetShaderLocation(sh, "viewPos");
        lightDir = GetShaderLocation(sh, "lightDir");
        fogColor = GetShaderLocation(sh, "fogColor");
        fogNear  = GetShaderLocation(sh, "fogNear");
        fogFar   = GetShaderLocation(sh, "fogFar");
        fogMax   = GetShaderLocation(sh, "fogMax");
        lit      = GetShaderLocation(sh, "lit");
        shine    = GetShaderLocation(sh, "shine");
    }
    // lit: shaded vs fog-only; shineAmount: specular strength. Uniforms are not batched, so geometry drawn
    // with the previous values is flushed first.
    void setMaterial(bool on, float shineAmount) const {
        if (!ok) return;
        rlDrawRenderBatchActive();
        const float v = on ? 1.0f : 0.0f;
        SetShaderValue(sh, lit, &v, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, shine, &shineAmount, SHADER_UNIFORM_FLOAT);
    }
    void setLit(bool on) const { setMaterial(on, 1.0f); }
};

// Procedural textures (made with OpenCV, uploaded with mipmaps + anisotropic filtering so far ground stays crisp).
Texture2D UploadTexture(const cv::Mat& bgr) {
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
    Image im{rgba.data, rgba.cols, rgba.rows, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(im);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

// Tileable noise: blur a 3x3 repeat of the pattern and keep the middle, so repeated textures have no seams.
cv::Mat Noise(int size, double sigma, double blurX, double blurY, unsigned seed) {
    cv::Mat n(size, size, CV_32F);
    cv::RNG rng(seed);
    rng.fill(n, cv::RNG::NORMAL, 0.0, 1.0);
    cv::Mat big;
    cv::repeat(n, 3, 3, big);
    cv::GaussianBlur(big, big, cv::Size(0, 0), blurX, blurY);
    n = big(cv::Rect(size, size, size, size)).clone();
    cv::normalize(n, n, -sigma, sigma, cv::NORM_MINMAX);
    return n;
}

cv::Mat AddNoise(const cv::Mat& bgr, const cv::Mat& n) {
    cv::Mat f; bgr.convertTo(f, CV_32FC3);
    std::vector<cv::Mat> ch; cv::split(f, ch);
    for (auto& c : ch) c += n;
    cv::merge(ch, f);
    cv::Mat out; f.convertTo(out, CV_8UC3);
    return out;
}

struct SceneTextures {
    Texture2D grass{}, stone{}, wood{}, rug{}, tiles{};
    void load() {
        const int S = 512;
        {   // grass: soft two-tone checker (mowed lawn stripes) + noise
            cv::Mat m(S, S, CV_8UC3);
            for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
                const bool a = ((x / 128) + (y / 128)) % 2 == 0;
                m.at<cv::Vec3b>(y, x) = a ? cv::Vec3b(60, 150, 80) : cv::Vec3b(52, 132, 70);
            }
            grass = UploadTexture(AddNoise(m, Noise(S, 22, 1.2, 1.2, 1)));
        }
        {   // stone paving: gray slabs with dark grout
            cv::Mat m(S, S, CV_8UC3, cv::Scalar(150, 152, 155));
            cv::RNG rng(5);
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
                const int v = rng.uniform(-18, 18);
                cv::rectangle(m, cv::Rect(c * 128 + 4, r * 128 + 4, 120, 120), cv::Scalar(165 + v, 168 + v, 170 + v), cv::FILLED);
            }
            stone = UploadTexture(AddNoise(m, Noise(S, 14, 1.0, 1.0, 2)));
        }
        {   // wood planks: 8 planks with staggered seams and long grain
            cv::Mat m(S, S, CV_8UC3);
            cv::RNG rng(9);
            for (int p = 0; p < 8; ++p) {
                const int v = rng.uniform(-22, 22);
                m(cv::Rect(0, p * 64, S, 64)).setTo(cv::Scalar(60 + v / 2, 105 + v, 150 + v));
                cv::line(m, cv::Point(0, p * 64), cv::Point(S, p * 64), cv::Scalar(35, 55, 80), 2);
                const int seam = (p * 197) % S;
                cv::line(m, cv::Point(seam, p * 64), cv::Point(seam, p * 64 + 63), cv::Scalar(35, 55, 80), 2);
            }
            wood = UploadTexture(AddNoise(m, Noise(S, 18, 18.0, 0.8, 3)));
        }
        {   // rug: deep red with gold border and a central medallion
            cv::Mat m(S, S, CV_8UC3, cv::Scalar(40, 30, 140));
            cv::rectangle(m, cv::Rect(16, 16, S - 32, S - 32), cv::Scalar(60, 170, 210), 18);
            cv::rectangle(m, cv::Rect(52, 52, S - 104, S - 104), cv::Scalar(90, 60, 30), 6);
            cv::ellipse(m, cv::Point(S / 2, S / 2), cv::Size(120, 80), 0, 0, 360, cv::Scalar(60, 170, 210), 10);
            cv::ellipse(m, cv::Point(S / 2, S / 2), cv::Size(60, 40), 0, 0, 360, cv::Scalar(90, 60, 30), cv::FILLED);
            rug = UploadTexture(AddNoise(m, Noise(S, 10, 0.8, 0.8, 4)));
        }
        {   // checker tiles (classic black/white floor)
            cv::Mat m(S, S, CV_8UC3);
            for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x)
                m.at<cv::Vec3b>(y, x) = ((x / 256) + (y / 256)) % 2 == 0 ? cv::Vec3b(225, 228, 230) : cv::Vec3b(45, 45, 50);
            tiles = UploadTexture(AddNoise(m, Noise(S, 8, 1.0, 1.0, 6)));
        }
    }
    void unload() { for (Texture2D* t : {&grass, &stone, &wood, &rug, &tiles}) if (t->id) UnloadTexture(*t); }
};

// --------------------------------------------------------- scene helpers ----
struct SceneCtx {
    float L, R, B, T, zG;       // screen opening in world units; glass plane z
    double time;
    const SceneShader* sh;
    const SceneTextures* tex;
    Vector3 lightDir;
};

void Lit(const SceneCtx& c, bool on) { c.sh->setLit(on); }
void Matte(const SceneCtx& c) { c.sh->setMaterial(true, 0.0f); }   // lit, no specular glare (ground, rugs)

void DrawQuad3D(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
}

void TexQuad(const Texture2D& tex, Vector3 a, Vector3 b, Vector3 c, Vector3 d, float u, float v, Color tint = WHITE) {
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlTexCoord2f(0, 0); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(u, 0); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(u, v); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(0, v); rlVertex3f(d.x, d.y, d.z);
    rlEnd();
    rlSetTexture(rlGetTextureIdDefault());   // rlSetTexture(0) alone would leave this texture bound for later quads
}

// Horizontal textured rectangle at height y, tile = world units per texture repeat.
void TexFloor(const Texture2D& tex, float x0, float x1, float z0, float z1, float y, float tile, Color tint = WHITE) {
    TexQuad(tex, {x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, (x1 - x0) / tile, (z0 - z1) / tile, tint);
}

void Box(float x0, float x1, float y0, float y1, float z0, float z1, Color c) {
    DrawCube({(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f}, std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0), c);
}

// The window's "reveal": the thickness of the wall around the opening. Moving your head shows its inner faces,
// and it hides and reveals the world behind it: the strongest cue that the screen is an opening.
void DrawReveal(const SceneCtx& c, float depth, Color sides, Color head, Color sill) {
    const float z0 = c.zG, z1 = c.zG - depth;
    DrawQuad3D({c.L, c.B, z0}, {c.L, c.B, z1}, {c.L, c.T, z1}, {c.L, c.T, z0}, sides);
    DrawQuad3D({c.R, c.B, z0}, {c.R, c.T, z0}, {c.R, c.T, z1}, {c.R, c.B, z1}, sides);
    DrawQuad3D({c.L, c.T, z0}, {c.L, c.T, z1}, {c.R, c.T, z1}, {c.R, c.T, z0}, head);
    DrawQuad3D({c.L, c.B, z0}, {c.R, c.B, z0}, {c.R, c.B, z1}, {c.L, c.B, z1}, sill);
}

void DrawGlassEdge(const SceneCtx& c, Color col) {
    DrawLine3D({c.L, c.B, c.zG}, {c.R, c.B, c.zG}, col);
    DrawLine3D({c.R, c.B, c.zG}, {c.R, c.T, c.zG}, col);
    DrawLine3D({c.R, c.T, c.zG}, {c.L, c.T, c.zG}, col);
    DrawLine3D({c.L, c.T, c.zG}, {c.L, c.B, c.zG}, col);
}

void DrawShadowDisc(Vector3 center, float r, Color col) {
    constexpr int kSeg = 28;
    for (int i = 0; i < kSeg; ++i) {
        const float a0 = 2.0f * PI * i / kSeg, a1 = 2.0f * PI * (i + 1) / kSeg;
        DrawTriangle3D(center, {center.x + r * std::cos(a1), center.y, center.z + r * std::sin(a1)},
                               {center.x + r * std::cos(a0), center.y, center.z + r * std::sin(a0)}, col);
    }
}

// Soft blob shadow on a floor at floorY, cast along the light direction from a point at `p`.
void DropShadow(Vector3 p, float size, float floorY, Vector3 light, float lift, float strength = 1.0f) {
    const float h = p.y - floorY;
    if (h < 0 || light.y >= -0.1f) return;
    const float t = h / -light.y;
    const Vector3 c{p.x + light.x * t, floorY + lift, p.z + light.z * t};
    const float r = size * (1.0f + h * 0.02f);
    const float fade = strength * std::clamp(1.0f - h / (size * 30.0f), 0.35f, 1.0f);
    DrawShadowDisc(c, r * 1.30f, Fade(BLACK, 0.07f * fade));
    DrawShadowDisc(c, r * 1.00f, Fade(BLACK, 0.10f * fade));
    DrawShadowDisc(c, r * 0.65f, Fade(BLACK, 0.13f * fade));
}

void BeginShadows() { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
void EndShadows()   { rlDrawRenderBatchActive(); rlEnableDepthMask(); }

void DrawGrid(float L, float R, float B, float T, float zF, float zB, float ringStep, float lineStep, Color c) {
    const float e = 0.02f;
    const int rings = std::max(1, int(std::lround((zF - zB) / ringStep)));
    for (int i = 1; i <= rings; ++i) {
        const float z = zF + (zB - zF) * float(i) / float(rings) + (i == rings ? e : 0.0f);
        DrawLine3D({L + e, B + e, z}, {R - e, B + e, z}, c);
        DrawLine3D({L + e, T - e, z}, {R - e, T - e, z}, c);
        DrawLine3D({L + e, B + e, z}, {L + e, T - e, z}, c);
        DrawLine3D({R - e, B + e, z}, {R - e, T - e, z}, c);
    }
    const int nx = std::max(2, int(std::lround((R - L) / lineStep)));
    for (int i = 1; i < nx; ++i) {
        const float x = L + (R - L) * float(i) / float(nx);
        DrawLine3D({x, B + e, zF}, {x, B + e, zB}, c);
        DrawLine3D({x, T - e, zF}, {x, T - e, zB}, c);
        DrawLine3D({x, B, zB + e}, {x, T, zB + e}, c);
    }
    const int ny = std::max(2, int(std::lround((T - B) / lineStep)));
    for (int j = 1; j < ny; ++j) {
        const float y = B + (T - B) * float(j) / float(ny);
        DrawLine3D({L + e, y, zF}, {L + e, y, zB}, c);
        DrawLine3D({R - e, y, zF}, {R - e, y, zB}, c);
        DrawLine3D({L, y, zB + e}, {R, y, zB + e}, c);
    }
}

void DrawBoxRoom(float L, float R, float B, float T, float zF, float zB, Color floor, Color walls, Color ceil, Color back) {
    DrawQuad3D({L, B, zF}, {R, B, zF}, {R, B, zB}, {L, B, zB}, floor);
    DrawQuad3D({L, T, zF}, {L, T, zB}, {R, T, zB}, {R, T, zF}, ceil);
    DrawQuad3D({L, B, zF}, {L, B, zB}, {L, T, zB}, {L, T, zF}, walls);
    DrawQuad3D({R, B, zF}, {R, T, zF}, {R, T, zB}, {R, B, zB}, walls);
    DrawQuad3D({L, B, zB}, {R, B, zB}, {R, T, zB}, {L, T, zB}, back);
}

void DrawPine(float x, float z, float groundY, float h, Color leaves) {
    DrawCylinder({x, groundY, z}, h * 0.035f, h * 0.05f, h * 0.35f, 10, Color{90, 62, 40, 255});
    for (int i = 0; i < 3; ++i) {
        const float y = groundY + h * (0.22f + 0.24f * i);
        const float r = h * (0.30f - 0.07f * i);
        DrawCylinder({x, y, z}, 0.0f, r, h * 0.42f, 14, leaves);
    }
}

void DrawRoundTree(float x, float z, float groundY, float h, Color leaves) {
    DrawCylinder({x, groundY, z}, h * 0.04f, h * 0.06f, h * 0.55f, 10, Color{96, 66, 44, 255});
    const float r = h * 0.28f;
    DrawSphereEx({x, groundY + h * 0.68f, z}, r, 10, 12, leaves);
    DrawSphereEx({x - r * 0.6f, groundY + h * 0.58f, z + r * 0.2f}, r * 0.75f, 8, 10, leaves);
    DrawSphereEx({x + r * 0.55f, groundY + h * 0.62f, z - r * 0.25f}, r * 0.8f, 8, 10, leaves);
}

// ------------------------------------------------------------------ scenes --
// 1. WINDOW: a real window (reveal, sash, mullions) onto a garden that is far wider than the screen.
//    Near flower box + mullions vs. far trees and hills = huge relative motion between depth layers.
void SceneWindow(const SceneCtx& c) {
    const float W = c.R - c.L, H = c.T - c.B;
    const float rd = 2.2f, zW = c.zG - rd;              // the glass of the real window sits at the back of the reveal
    const float groundY = c.B - 9.0f;
    const Vector3 L = c.lightDir;

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawReveal(c, rd, Color{232, 226, 214, 255}, Color{220, 214, 202, 255}, Color{196, 160, 118, 255});
    // Sash frame and mullions (window bars) at the back of the reveal.
    const Color sash{244, 244, 238, 255};
    const float fw = 0.45f, fd = 0.5f;
    Box(c.L, c.L + fw, c.B, c.T, zW, zW - fd, sash);
    Box(c.R - fw, c.R, c.B, c.T, zW, zW - fd, sash);
    Box(c.L, c.R, c.T - fw, c.T, zW, zW - fd, sash);
    Box(c.L, c.R, c.B, c.B + fw * 1.4f, zW, zW - fd, sash);
    Box(-0.18f, 0.18f, c.B, c.T, zW, zW - fd, sash);
    const float barY = c.B + H * 0.62f;
    Box(c.L, c.R, barY - 0.16f, barY + 0.16f, zW, zW - fd, sash);

    const float zOut = zW - fd - 0.2f;
    // Ground, path, flower box right outside the window.
    Matte(c);
    TexFloor(c.tex->grass, -2500, 2500, zOut, -4000, groundY, 22.0f);
    TexFloor(c.tex->stone, -5.5f, 5.5f, zOut - 14.0f, -420.0f, groundY + 0.25f, 11.0f);
    Lit(c, true);
    Box(-W * 0.42f, W * 0.42f, c.B - 3.4f, c.B - 0.7f, zOut - 0.4f, zOut - 4.0f, Color{170, 92, 60, 255});
    Box(-W * 0.40f, W * 0.40f, c.B - 0.75f, c.B - 0.6f, zOut - 0.6f, zOut - 3.8f, Color{70, 50, 36, 255});
    const Color flowers[] = {Color{230, 60, 80, 255}, Color{250, 200, 40, 255}, Color{240, 240, 245, 255},
                             Color{170, 80, 200, 255}, Color{250, 130, 50, 255}};
    const int nFlowers = std::max(6, int(W / 1.4f));
    for (int i = 0; i < nFlowers; ++i) {
        const float x = -W * 0.38f + (W * 0.76f) * float(i) / float(nFlowers - 1);
        const float z = zOut - 1.2f - 1.4f * float((i * 7) % 3) / 2.0f;
        const float h = 0.9f + 0.5f * float((i * 5) % 4) / 3.0f;
        DrawSphereEx({x, c.B - 0.4f + h, z}, 0.42f, 8, 10, flowers[i % 5]);
        DrawSphereEx({x + 0.35f, c.B - 0.6f + h * 0.6f, z - 0.3f}, 0.35f, 6, 8, Color{60, 140, 60, 255});
    }
    // Hedges either side of the path.
    for (float sx : {-1.0f, 1.0f}) Box(sx * 9.0f, sx * 220.0f, groundY, groundY + 6.0f, -38.0f, -44.0f, Color{46, 112, 58, 255});
    // Fence.
    const float fz = -110.0f;
    for (float x = -300; x <= 300; x += 14) {
        if (std::abs(x) < 6) continue;
        Box(x - 0.45f, x + 0.45f, groundY, groundY + 9.0f, fz + 0.45f, fz - 0.45f, Color{235, 232, 222, 255});
    }
    for (float y : {3.0f, 7.0f}) {
        Box(-300, -6, groundY + y - 0.3f, groundY + y + 0.3f, fz + 0.2f, fz - 0.2f, Color{230, 226, 214, 255});
        Box(6, 300, groundY + y - 0.3f, groundY + y + 0.3f, fz + 0.2f, fz - 0.2f, Color{230, 226, 214, 255});
    }
    // Lamp post close to the window: a tall near vertical that slides across the far scenery.
    const float lx = W * 0.32f + 6.0f, lz = -26.0f;
    DrawCylinder({lx, groundY, lz}, 0.35f, 0.45f, 34.0f, 12, Color{40, 44, 48, 255});
    Box(lx - 2.6f, lx + 0.2f, groundY + 33.6f, groundY + 34.2f, lz + 0.2f, lz - 0.2f, Color{40, 44, 48, 255});
    DrawCylinder({lx - 2.4f, groundY + 31.0f, lz}, 0.6f, 1.4f, 2.6f, 12, Color{40, 44, 48, 255});
    // Bench.
    Box(-34, -20, groundY + 4.0f, groundY + 4.6f, -92, -96, Color{140, 96, 60, 255});
    Box(-34, -20, groundY + 4.6f, groundY + 8.6f, -95.6f, -96.2f, Color{140, 96, 60, 255});
    for (float x : {-33.0f, -21.0f}) Box(x - 0.4f, x + 0.4f, groundY, groundY + 4.0f, -92.5f, -95.5f, Color{50, 50, 55, 255});
    // Trees at many depths.
    struct T3 { float x, z, h; bool pine; };
    static const T3 trees[] = {
        {-40, -70, 42, false}, {48, -88, 50, true}, {-95, -150, 60, true}, {120, -170, 55, false}, {-20, -240, 70, true},
        {-200, -260, 65, false}, {210, -300, 80, true}, {60, -380, 75, false}, {-140, -420, 90, true}, {320, -520, 95, false},
        {-330, -600, 100, true}, {20, -700, 110, true}, {-60, -900, 120, false}, {450, -950, 130, true}, {-520, -1100, 140, true},
        {180, -1300, 150, false}};
    for (const auto& t : trees)
        t.pine ? DrawPine(t.x, t.z, groundY, t.h, Color{38, 104, 60, 255}) : DrawRoundTree(t.x, t.z, groundY, t.h, Color{70, 140, 60, 255});
    // Hills and clouds far away (fog blends them into the sky).
    DrawCylinder({-900, groundY, -2600}, 0.0f, 900, 520, 24, Color{90, 120, 110, 255});
    DrawCylinder({600, groundY, -3000}, 0.0f, 1100, 700, 24, Color{100, 128, 125, 255});
    DrawCylinder({1800, groundY, -2400}, 0.0f, 700, 380, 24, Color{95, 125, 112, 255});
    for (int i = 0; i < 7; ++i) {
        const float x = -900.0f + 320.0f * i, y = groundY + 260.0f + 40.0f * float(i % 3), z = -1500.0f - 150.0f * float(i % 2);
        for (int k = 0; k < 4; ++k)
            DrawSphereEx({x + 45.0f * k, y + 12.0f * float(k % 2), z}, 45.0f + 10.0f * float(k % 2), 8, 10, Color{250, 250, 252, 255});
    }

    Lit(c, false);
    DrawGlassEdge(c, Color{30, 30, 30, 255});
    BeginShadows();
    for (const auto& t : trees) DropShadow({t.x, groundY + t.h * 0.6f, t.z}, t.h * 0.22f, groundY, L, 0.3f, 1.2f);
    DropShadow({lx, groundY + 20.0f, lz}, 2.0f, groundY, L, 0.3f);
    DropShadow({-27, groundY + 4.0f, -94}, 6.0f, groundY, L, 0.3f);
    EndShadows();
}

// 2. TARGETS: Johnny Lee's classic Wii head-tracking demo. A deep gridded box with targets on sticks;
//    a few targets float in front of the glass (kept near the center so the screen edge never cuts them).
void SceneTargets(const SceneCtx& c) {
    const float zB = c.zG - 38.0f;
    struct Tg { float x, y, z, r; };
    static const Tg targets[] = {
        {-4.5f, 2.6f, -4.0f, 1.3f}, {3.8f, -1.8f, -10.0f, 1.5f}, {-1.5f, -2.6f, -18.0f, 1.6f}, {5.0f, 3.2f, -22.0f, 1.4f},
        {-6.0f, -0.8f, -28.0f, 1.8f}, {1.2f, 3.8f, -32.0f, 1.6f}, {0.6f, 0.4f, 4.5f, 0.8f}, {-1.6f, 1.8f, 2.8f, 0.7f},
        {2.2f, 1.4f, -1.0f, 1.0f}, {-3.2f, 4.2f, -14.0f, 1.2f}};

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawBoxRoom(c.L, c.R, c.B, c.T, c.zG, zB, Color{18, 18, 24, 255}, Color{14, 14, 20, 255}, Color{16, 16, 22, 255},
                Color{12, 12, 18, 255});
    for (const auto& t : targets) {
        const Color ring[] = {Color{220, 30, 40, 255}, WHITE, Color{220, 30, 40, 255}, WHITE, Color{220, 30, 40, 255}};
        for (int k = 0; k < 5; ++k) {
            const float rad = t.r * (1.0f - 0.19f * k), z = t.z + 0.03f * k;
            DrawCylinderEx({t.x, t.y, z}, {t.x, t.y, z + 0.12f}, rad, rad, 40, ring[k]);
        }
    }
    Lit(c, false);
    DrawGrid(c.L, c.R, c.B, c.T, c.zG, zB, 3.0f, 1.5f, Color{120, 180, 255, 255});
    for (const auto& t : targets) DrawLine3D({t.x, t.y, t.z}, {t.x, t.y, zB + 0.02f}, Color{200, 200, 210, 255});
    DrawGlassEdge(c, Color{200, 220, 255, 255});
}

// 3. ROOM: a furnished room much larger than the opening, seen through a doorway-like reveal. Familiar-size
//    objects (table, chairs, shelves, lamp) let your brain judge distance; edges of the opening hide and reveal them.
void SceneRoom(const SceneCtx& c) {
    const float W = c.R - c.L;
    const float rd = 1.2f, zF = c.zG - rd, zBk = c.zG - 120.0f;
    const float x0 = c.L - W * 1.3f, x1 = c.R + W * 1.3f;
    const float floorY = c.B - 7.0f, ceilY = c.T + 16.0f;
    const Vector3 L = c.lightDir;
    const Color wall{214, 196, 172, 255}, backWall{205, 186, 162, 255}, trim{92, 64, 44, 255};

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawReveal(c, rd, Color{110, 78, 54, 255}, Color{100, 70, 48, 255}, Color{120, 86, 60, 255});
    // Doorway in the back wall leading into a hallway: real depth beyond the room.
    const float dx = -26.0f, dHalf = 8.5f, dTop = floorY + 30.0f, hallEnd = zBk - 45.0f;
    Matte(c);
    TexFloor(c.tex->wood, x0, x1, zF, zBk, floorY, 16.0f);
    TexFloor(c.tex->wood, dx - dHalf, dx + dHalf, zBk, hallEnd, floorY, 16.0f, Color{170, 160, 150, 255});
    TexQuad(c.tex->rug, {-16, floorY + 0.05f, -28}, {16, floorY + 0.05f, -28}, {16, floorY + 0.05f, -66}, {-16, floorY + 0.05f, -66}, 1, 1);
    Lit(c, true);
    DrawQuad3D({x0, ceilY, zF}, {x0, ceilY, zBk}, {x1, ceilY, zBk}, {x1, ceilY, zF}, Color{238, 234, 226, 255});
    DrawQuad3D({x0, floorY, zF}, {x0, floorY, zBk}, {x0, ceilY, zBk}, {x0, ceilY, zF}, wall);
    DrawQuad3D({x1, floorY, zF}, {x1, ceilY, zF}, {x1, ceilY, zBk}, {x1, floorY, zBk}, wall);
    DrawQuad3D({x0, floorY, zBk}, {dx - dHalf, floorY, zBk}, {dx - dHalf, ceilY, zBk}, {x0, ceilY, zBk}, backWall);
    DrawQuad3D({dx + dHalf, floorY, zBk}, {x1, floorY, zBk}, {x1, ceilY, zBk}, {dx + dHalf, ceilY, zBk}, backWall);
    DrawQuad3D({dx - dHalf, dTop, zBk}, {dx + dHalf, dTop, zBk}, {dx + dHalf, ceilY, zBk}, {dx - dHalf, ceilY, zBk}, backWall);
    const Color hall{150, 132, 112, 255};
    DrawQuad3D({dx - dHalf, floorY, zBk}, {dx - dHalf, floorY, hallEnd}, {dx - dHalf, dTop, hallEnd}, {dx - dHalf, dTop, zBk}, hall);
    DrawQuad3D({dx + dHalf, floorY, zBk}, {dx + dHalf, dTop, zBk}, {dx + dHalf, dTop, hallEnd}, {dx + dHalf, floorY, hallEnd}, hall);
    DrawQuad3D({dx - dHalf, dTop, zBk}, {dx - dHalf, dTop, hallEnd}, {dx + dHalf, dTop, hallEnd}, {dx + dHalf, dTop, zBk}, hall);
    DrawQuad3D({dx - dHalf, floorY, hallEnd}, {dx + dHalf, floorY, hallEnd}, {dx + dHalf, dTop, hallEnd}, {dx - dHalf, dTop, hallEnd},
               Color{120, 104, 88, 255});
    Box(dx - 5.0f, dx + 5.0f, floorY + 12.0f, floorY + 24.0f, hallEnd + 0.3f, hallEnd, Color{60, 90, 130, 255});   // picture at the end
    // Baseboards and ceiling beams.
    Box(x0, x0 + 0.5f, floorY, floorY + 1.8f, zF, zBk, trim);
    Box(x1 - 0.5f, x1, floorY, floorY + 1.8f, zF, zBk, trim);
    Box(x0, dx - dHalf, floorY, floorY + 1.8f, zBk + 0.5f, zBk, trim);
    Box(dx + dHalf, x1, floorY, floorY + 1.8f, zBk + 0.5f, zBk, trim);
    for (float x = x0 + 14.0f; x < x1 - 4.0f; x += 18.0f) Box(x - 0.8f, x + 0.8f, ceilY - 1.8f, ceilY, zF, zBk, Color{120, 88, 62, 255});
    // Table and chairs.
    const float tx = -2.0f, tz = -46.0f, ty = floorY + 15.0f;
    Box(tx - 13, tx + 13, ty - 0.9f, ty, tz + 7, tz - 7, Color{150, 98, 58, 255});
    for (float sx : {-1.0f, 1.0f}) for (float sz : {-1.0f, 1.0f})
        Box(tx + sx * 11.5f - 0.6f, tx + sx * 11.5f + 0.6f, floorY, ty - 0.9f, tz + sz * 5.5f + 0.6f, tz + sz * 5.5f - 0.6f, Color{120, 78, 46, 255});
    auto chair = [&](float x, float z, float facing) {
        const float sy = floorY + 9.0f;
        Box(x - 3.5f, x + 3.5f, sy - 0.8f, sy, z + 3.5f, z - 3.5f, Color{170, 60, 50, 255});
        const float bz = z + facing * 3.2f;
        Box(x - 3.5f, x + 3.5f, sy, sy + 9.0f, bz + 0.4f, bz - 0.4f, Color{160, 55, 46, 255});
        for (float sx : {-1.0f, 1.0f}) for (float sz : {-1.0f, 1.0f})
            Box(x + sx * 3.0f - 0.35f, x + sx * 3.0f + 0.35f, floorY, sy - 0.8f, z + sz * 3.0f + 0.35f, z + sz * 3.0f - 0.35f, Color{90, 60, 40, 255});
    };
    chair(tx - 6.0f, tz + 11.5f, 1.0f);
    chair(tx + 7.0f, tz - 11.5f, -1.0f);
    // Teapot-ish objects on the table.
    DrawSphereEx({tx - 3, ty + 1.6f, tz + 1}, 1.6f, 12, 14, Color{60, 110, 170, 255});
    DrawCylinder({tx + 4, ty, tz - 2}, 0.9f, 0.9f, 2.6f, 14, Color{240, 240, 235, 255});
    // Bookshelf on the back wall.
    const float bx = 24.0f, bw = 26.0f, bh = 34.0f, bd = 5.0f;
    const Color shelfWood{110, 72, 44, 255};
    Box(bx - bw / 2, bx + bw / 2, floorY, floorY + bh, zBk + 0.5f, zBk + 0.01f, shelfWood);              // back panel
    Box(bx - bw / 2, bx - bw / 2 + 0.8f, floorY, floorY + bh, zBk + bd, zBk + 0.01f, shelfWood);         // sides
    Box(bx + bw / 2 - 0.8f, bx + bw / 2, floorY, floorY + bh, zBk + bd, zBk + 0.01f, shelfWood);
    Box(bx - bw / 2, bx + bw / 2, floorY + bh - 0.8f, floorY + bh, zBk + bd, zBk + 0.01f, shelfWood);    // top
    static std::vector<std::pair<Vector3, Vector3>> books;   // (min corner, size) for each book, generated once
    static std::vector<Color> bookColors;
    if (books.empty()) {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(0, 1);
        const Color pal[] = {Color{170, 40, 40, 255}, Color{40, 80, 150, 255}, Color{40, 120, 70, 255}, Color{200, 160, 60, 255},
                             Color{90, 50, 120, 255}, Color{230, 225, 210, 255}, Color{30, 30, 35, 255}};
        for (int shelf = 0; shelf < 4; ++shelf) {
            float x = bx - bw / 2 + 1.0f;
            const float y = floorY + 1.0f + shelf * 8.2f;
            while (x < bx + bw / 2 - 2.0f) {
                const float w = 0.8f + 0.9f * u(rng), h = 4.5f + 2.5f * u(rng);
                if (u(rng) > 0.12f) { books.push_back({{x, y, zBk + 0.6f}, {w * 0.92f, h, 3.6f + 0.6f * u(rng)}});
                                      bookColors.push_back(pal[int(u(rng) * 7) % 7]); }
                x += w;
            }
        }
    }
    for (int shelf = 0; shelf <= 4; ++shelf) {
        const float y = floorY + 1.0f + shelf * 8.2f;
        Box(bx - bw / 2 + 0.6f, bx + bw / 2 - 0.6f, y - 0.5f, y, zBk + bd, zBk + 0.3f, Color{130, 88, 54, 255});
    }
    for (size_t i = 0; i < books.size(); ++i) {
        const auto& b = books[i];
        Box(b.first.x, b.first.x + b.second.x, b.first.y, b.first.y + b.second.y, b.first.z, b.first.z + b.second.z, bookColors[i]);
    }
    // Door trim around the hallway opening.
    Box(dx - 9.5f, dx - 8.0f, floorY, floorY + 31.5f, zBk + 0.8f, zBk, trim);
    Box(dx + 8.0f, dx + 9.5f, floorY, floorY + 31.5f, zBk + 0.8f, zBk, trim);
    Box(dx - 9.5f, dx + 9.5f, floorY + 30.0f, floorY + 31.5f, zBk + 0.8f, zBk, trim);
    // Painting on the left wall.
    const float pz = -60.0f, py = floorY + 26.0f;
    Box(x0, x0 + 0.6f, py - 7.5f, py + 7.5f, pz + 10.5f, pz - 10.5f, Color{150, 110, 40, 255});
    DrawQuad3D({x0 + 0.62f, py, pz + 9.5f}, {x0 + 0.62f, py, pz - 9.5f}, {x0 + 0.62f, py + 6.5f, pz - 9.5f}, {x0 + 0.62f, py + 6.5f, pz + 9.5f}, Color{120, 170, 220, 255});
    DrawQuad3D({x0 + 0.62f, py - 6.5f, pz + 9.5f}, {x0 + 0.62f, py - 6.5f, pz - 9.5f}, {x0 + 0.62f, py, pz - 9.5f}, {x0 + 0.62f, py, pz + 9.5f}, Color{80, 140, 70, 255});
    // Plant near the front right and a floor lamp near the front left: partly hidden by the opening's edges.
    const float px = c.R + 3.0f, pzz = zF - 9.0f;
    DrawCylinder({px, floorY, pzz}, 2.0f, 2.6f, 5.5f, 16, Color{190, 100, 70, 255});
    for (int k = 0; k < 7; ++k) {
        const float a = k * 0.9f;
        DrawSphereEx({px + 2.2f * std::cos(a), floorY + 9.0f + 2.5f * float(k % 3), pzz + 2.2f * std::sin(a)}, 2.3f, 8, 10, Color{50, 130, 60, 255});
    }
    const float fx = c.L - 4.0f, fz = zF - 14.0f;
    DrawCylinder({fx, floorY, fz}, 1.6f, 1.6f, 0.6f, 14, Color{40, 40, 45, 255});
    DrawCylinder({fx, floorY, fz}, 0.25f, 0.25f, 30.0f, 8, Color{40, 40, 45, 255});
    DrawCylinder({fx, floorY + 28.0f, fz}, 3.2f, 1.8f, 4.0f, 18, Color{235, 220, 190, 255});

    Lit(c, false);
    // Pendant lamp near the front: cord + glowing bulb under a shade.
    const Vector3 lamp{1.5f, c.T + 2.0f, zF - 16.0f};
    DrawLine3D({lamp.x, ceilY, lamp.z}, {lamp.x, lamp.y + 2.5f, lamp.z}, Color{30, 30, 30, 255});
    DrawSphereEx({lamp.x, lamp.y, lamp.z}, 1.0f, 10, 12, Color{255, 236, 170, 255});
    Lit(c, true);
    DrawCylinder({lamp.x, lamp.y - 0.4f, lamp.z}, 0.6f, 3.4f, 3.0f, 20, Color{40, 70, 60, 255});
    Lit(c, false);
    // A window on the right wall showing bright sky.
    DrawQuad3D({x1 - 0.05f, floorY + 14.0f, -28.0f}, {x1 - 0.05f, floorY + 32.0f, -28.0f}, {x1 - 0.05f, floorY + 32.0f, -48.0f},
               {x1 - 0.05f, floorY + 14.0f, -48.0f}, Color{190, 220, 250, 255});
    BeginShadows();
    DropShadow({tx, floorY + 14.0f, tz}, 12.0f, floorY, L, 0.06f, 1.4f);
    DropShadow({tx - 6.0f, floorY + 8.0f, tz + 11.5f}, 4.0f, floorY, L, 0.07f);
    DropShadow({tx + 7.0f, floorY + 8.0f, tz - 11.5f}, 4.0f, floorY, L, 0.07f);
    DropShadow({px, floorY + 6.0f, pzz}, 3.5f, floorY, L, 0.06f);
    EndShadows();
}

// 4. SPACE: a porthole onto an asteroid field. Asteroids right behind the glass, a station and moon in the middle
//    distance, a ringed planet and stars far away. Everything slowly rotates.
void SceneSpace(const SceneCtx& c) {
    struct Rock { Vector3 p; float r; Vector3 axis; float speed, phase; Color col; Vector3 squash; };
    struct Star { Vector3 p; float s; Color col; };
    static std::vector<Rock> rocks;
    static std::vector<Star> stars;
    if (rocks.empty()) {
        std::mt19937 rng(21);
        std::uniform_real_distribution<float> u(0, 1);
        // A sparse field: a few small rocks just behind the glass, more further out, with clear lines of sight.
        for (int i = 0; i < 30; ++i) {
            const float z = -6.0f - 260.0f * u(rng) * u(rng);
            const float spread = 8.0f + std::abs(z) * 0.9f;
            Rock r;
            r.p = {(u(rng) * 2 - 1) * spread, 1.0f + (u(rng) * 2 - 1) * spread * 0.6f, c.zG + z};
            r.r = (0.35f + 1.2f * u(rng)) * (1.0f + std::abs(z) / 60.0f);
            r.axis = Vector3Normalize({u(rng) - 0.5f, u(rng) - 0.5f, u(rng) - 0.5f});
            r.speed = 8.0f + 30.0f * u(rng); r.phase = 360.0f * u(rng);
            const unsigned char g = (unsigned char)(95 + 60 * u(rng));
            r.col = Color{g, (unsigned char)(g * 0.9f), (unsigned char)(g * 0.8f), 255};
            r.squash = {0.8f + 0.4f * u(rng), 0.6f + 0.3f * u(rng), 0.8f + 0.4f * u(rng)};
            rocks.push_back(r);
        }
        for (int i = 0; i < 2200; ++i) {
            const float z = -300.0f - 3500.0f * u(rng);
            Star s;
            s.p = {(u(rng) * 2 - 1) * std::abs(z) * 1.1f, (u(rng) * 2 - 1) * std::abs(z) * 0.7f, z};
            s.s = std::abs(z) * (0.0012f + 0.0018f * u(rng));
            const unsigned char b = (unsigned char)(170 + 85 * u(rng));
            s.col = u(rng) > 0.85f ? Color{b, (unsigned char)(b * 0.85f), (unsigned char)(b * 0.7f), 255} : Color{b, b, 255, 255};
            stars.push_back(s);
        }
    }
    const float t = float(c.time);

    Lit(c, false);
    for (const auto& s : stars) DrawCube(s.p, s.s, s.s, s.s, s.col);
    const Vector3 planet{-420.0f, 140.0f, -1900.0f};

    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawReveal(c, 1.6f, Color{70, 74, 84, 255}, Color{62, 66, 76, 255}, Color{80, 84, 94, 255});
    DrawSphereEx(planet, 250.0f, 40, 48, Color{215, 150, 90, 255});
    DrawSphereEx({260.0f, -90.0f, -620.0f}, 48.0f, 24, 28, Color{170, 170, 175, 255});
    // Planet ring: a tilted flat annulus (after the planet, so the planet hides its far half).
    Lit(c, false);
    {
        const float r0 = 330.0f, r1 = 470.0f;
        const float tilt = 20.0f * DEG2RAD, roll = -15.0f * DEG2RAD;   // ring plane tipped toward us, then rolled
        auto ringPt = [&](float r, float a) {
            const float x = r * std::cos(a), z0 = r * std::sin(a);
            const float y1 = -z0 * std::sin(tilt), z1 = z0 * std::cos(tilt);
            return Vector3{planet.x + x * std::cos(roll) - y1 * std::sin(roll),
                           planet.y + x * std::sin(roll) + y1 * std::cos(roll), planet.z + z1};
        };
        constexpr int kSeg = 96;
        for (int i = 0; i < kSeg; ++i) {
            const float a0 = 2.0f * PI * i / kSeg, a1 = 2.0f * PI * (i + 1) / kSeg;
            const Color col = Fade(i % 2 ? Color{215, 195, 150, 255} : Color{200, 180, 140, 255}, 0.55f);
            DrawQuad3D(ringPt(r0, a0), ringPt(r1, a0), ringPt(r1, a1), ringPt(r0, a1), col);
        }
    }
    Lit(c, true);
    // Space station: a rotating hub with arms and solar panels, in the middle distance.
    rlPushMatrix();
    rlTranslatef(-30.0f, 14.0f, c.zG - 120.0f);
    rlRotatef(t * 6.0f, 0, 0, 1);
    DrawCylinderEx({-6, 0, 0}, {6, 0, 0}, 1.4f, 1.4f, 16, Color{210, 212, 220, 255});
    DrawSphereEx({0, 0, 0}, 2.4f, 14, 16, Color{230, 230, 236, 255});
    for (float sx : {-1.0f, 1.0f}) {
        Box(sx * 7.0f - 0.2f, sx * 7.0f + 0.2f, -9, 9, 0.2f, -0.2f, Color{200, 200, 210, 255});
        Box(sx * 7.0f - 2.6f, sx * 7.0f + 2.6f, 2.5f, 8.5f, 0.1f, -0.1f, Color{40, 70, 150, 255});
        Box(sx * 7.0f - 2.6f, sx * 7.0f + 2.6f, -8.5f, -2.5f, 0.1f, -0.1f, Color{40, 70, 150, 255});
    }
    rlPopMatrix();
    for (const auto& r : rocks) {
        rlPushMatrix();
        rlTranslatef(r.p.x, r.p.y, r.p.z);
        rlRotatef(r.phase + t * r.speed, r.axis.x, r.axis.y, r.axis.z);
        rlScalef(r.squash.x, r.squash.y, r.squash.z);
        DrawSphereEx({0, 0, 0}, r.r, 4, 6, r.col);   // low-poly sphere = faceted rock
        rlPopMatrix();
    }
    Lit(c, false);
    DrawGlassEdge(c, Color{120, 125, 135, 255});
}

// 5. SHAPES: the original floating shapes in a gridded room, with shadows and hanging strings.
void SceneShapes(const SceneCtx& c) {
    struct Shape { int type; Vector3 p; float size; Color col; };
    static const Shape shapes[] = {
        {0, {-4.0f, 2.5f, -3.0f}, 1.4f, RED},      {1, {-1.5f, 3.6f, -8.0f}, 1.6f, BLUE},
        {2, {3.0f, 1.5f, -14.0f}, 2.0f, GREEN},    {3, {4.0f, -1.5f, -5.0f}, 1.6f, ORANGE},
        {4, {-3.5f, -1.0f, -11.0f}, 1.8f, PURPLE}, {1, {0.0f, 0.6f, 3.2f}, 0.9f, GOLD},
        {0, {1.0f, 3.0f, -20.0f}, 2.2f, SKYBLUE},  {2, {-4.4f, 0.8f, -18.0f}, 2.0f, PINK},
        {3, {2.2f, -2.8f, -9.0f}, 1.6f, LIME},     {4, {1.6f, -0.6f, 1.9f}, 0.9f, VIOLET},
        {0, {-1.8f, -2.9f, -2.0f}, 1.2f, MAROON},  {1, {-0.5f, -2.2f, -16.0f}, 1.8f, DARKBLUE},
        {2, {4.2f, 3.6f, -10.0f}, 1.4f, DARKGREEN},{3, {-2.6f, 1.4f, -0.5f}, 1.0f, BROWN},
        {4, {3.5f, -2.4f, -22.0f}, 2.0f, DARKPURPLE}};
    const float zB = c.zG - 25.0f;
    Lit(c, true);
    rlDrawRenderBatchActive();
    DrawBoxRoom(c.L, c.R, c.B, c.T, c.zG, zB, Color{196, 198, 206, 255}, Color{222, 224, 232, 255}, Color{236, 237, 242, 255},
                Color{210, 212, 220, 255});
    for (const auto& s : shapes) {
        const Vector3 p{s.p.x, s.p.y, s.p.z + c.zG - 1.0f};
        switch (s.type) {
            case 0: DrawCube(p, s.size, s.size, s.size, s.col); break;
            case 1: DrawSphereEx(p, s.size * 0.5f, 24, 24, s.col); break;
            case 2: DrawCylinder({p.x, p.y - s.size * 0.5f, p.z}, s.size * 0.35f, s.size * 0.35f, s.size, 24, s.col); break;
            case 3: DrawCylinder({p.x, p.y - s.size * 0.5f, p.z}, 0.0f, s.size * 0.45f, s.size, 24, s.col); break;
            default: DrawCapsule({p.x, p.y - s.size * 0.3f, p.z}, {p.x, p.y + s.size * 0.3f, p.z}, s.size * 0.28f, 16, 12, s.col);
        }
    }
    Lit(c, false);
    DrawGrid(c.L, c.R, c.B, c.T, c.zG, zB, 2.5f, 1.25f, Color{118, 122, 136, 255});
    DrawGlassEdge(c, Color{25, 25, 30, 255});
    for (const auto& s : shapes) {
        const Vector3 p{s.p.x, s.p.y, s.p.z + c.zG - 1.0f};
        if (p.z > c.zG - 0.2f) continue;
        DrawLine3D({p.x, p.y + s.size * 0.5f, p.z}, {p.x, c.T, p.z}, Color{80, 82, 95, 255});
    }
    BeginShadows();
    for (const auto& s : shapes) {
        const Vector3 p{s.p.x, s.p.y, s.p.z + c.zG - 1.0f};
        if (p.z > c.zG - 0.5f) continue;
        DropShadow(p, s.size * 0.55f, c.B, c.lightDir, 0.02f);
    }
    EndShadows();
}

struct SceneDef {
    const char* name;
    Color sky;
    Vector3 fog;              // 0..1 rgb
    float fogStart, fogEnd, fogMax;
    Vector3 light;            // direction the light travels
    void (*draw)(const SceneCtx&);
};

const SceneDef kScenes[] = {
    {"Window onto a garden", Color{150, 198, 240, 255}, {0.80f, 0.87f, 0.93f}, 60.0f, 3200.0f, 0.92f, {-0.45f, -1.0f, -0.55f}, SceneWindow},
    {"Targets (Johnny Lee tribute)", Color{8, 8, 12, 255}, {0.0f, 0.0f, 0.0f}, 4.0f, 70.0f, 0.65f, {0.3f, -1.0f, -0.5f}, SceneTargets},
    {"Furnished room", Color{30, 24, 20, 255}, {0.24f, 0.18f, 0.14f}, 25.0f, 320.0f, 0.35f, {-0.35f, -1.0f, -0.45f}, SceneRoom},
    {"Space porthole", Color{2, 2, 8, 255}, {0.0f, 0.0f, 0.0f}, 1e5f, 2e5f, 0.0f, {-1.0f, -0.25f, -0.35f}, SceneSpace},
    {"Floating shapes", Color{30, 30, 36, 255}, {0.55f, 0.57f, 0.63f}, 0.0f, 32.0f, 0.55f, {0.35f, -1.0f, -0.45f}, SceneShapes},
};
constexpr int kSceneCount = int(sizeof(kScenes) / sizeof(kScenes[0]));

// --------------------------------------------------------------- UI bits ----
struct NumberField {
    std::string text;
    bool fresh = true;
    void set(double v, int decimals) { text = cv::format("%.*f", decimals, v); fresh = true; }
    void update() {
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
            const bool digit = c >= '0' && c <= '9';
            const bool dot = c == '.';
            if (!digit && !dot) continue;
            if (fresh) { text.clear(); fresh = false; }
            if (dot && text.find('.') != std::string::npos) continue;
            if (text.size() < 9) text.push_back(char(c));
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !text.empty()) { text.pop_back(); fresh = false; }
    }
    double value() const { try { return text.empty() ? 0.0 : std::stod(text); } catch (...) { return 0.0; } }
};

bool EnterPressed() { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER); }

void DrawPanel(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, Fade(BLACK, 0.78f));
    DrawRectangleLines(x, y, w, h, Fade(RAYWHITE, 0.4f));
}

int DrawLinesAt(const std::vector<std::string>& lines, int x, int y, int size, Color c) {
    for (const auto& l : lines) { DrawText(l.c_str(), x, y, size, c); y += size + 8; }
    return y;
}

struct PhotoState {
    bool loaded = false;
    Texture2D tex{};
    std::vector<Vector2> pts;
    int stage = 0;
    std::string name;
};

struct PhotoResult {
    double wMm = 0, hMm = 0, camX = 0, camY = 0, aspect = 0, photoAspect = 0;
    std::vector<std::string> warnings;
};

bool LoadPhoto(const std::string& path, PhotoState& ps, std::string& err) {
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
PhotoResult ComputePhoto(const std::vector<Vector2>& p, double diagIn, double aspect) {
    PhotoResult r;
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
        r.warnings.push_back(cv::format("Photo shape %.2f:1 vs screen %.2f:1. Retake more straight-on, or check the corners.",
                                        r.photoAspect, aspect));
    if (std::abs(r.camX) < r.wMm / 2 && std::abs(r.camY) < r.hMm / 2)
        r.warnings.push_back("The webcam point is inside the screen area. Check the clicks.");
    if (std::abs(r.camX) > r.wMm || std::abs(r.camY) > r.hMm)
        r.warnings.push_back("The webcam point is very far from the screen. Check the clicks.");
    return r;
}
} // namespace


// ------------------------------------------------------------------ main ----
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const float screenHeight = kScreenHeightUnits;

    // ---- dlib landmark model ----
    fs::path model = fs::current_path() / "custom/shape_predictor_68_face_landmarks.dat";
    if (!fs::exists(model))
        model = fs::current_path() / "build/combined/bin/Release/custom/shape_predictor_68_face_landmarks.dat";
    if (!fs::exists(model)) { std::cerr << "Missing dlib predictor: " << model << "\n"; return 1; }
    dlib::shape_predictor predictor;
    try { dlib::deserialize(model.string()) >> predictor; }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }

    // ---- YuNet model ----
    fs::path yunetModel = kYuNetModelAbs;
    if (!fs::exists(yunetModel)) yunetModel = fs::current_path() / "custom/face_detection_yunet_2023mar.onnx";
    if (!fs::exists(yunetModel)) yunetModel = fs::current_path() / "build/combined/bin/Release/custom/face_detection_yunet_2023mar.onnx";
    if (!fs::exists(yunetModel)) { std::cerr << "Missing YuNet model: " << yunetModel << "\n"; return 1; }

    // ---- config ----
    const std::string configPath = fs::absolute(kConfigFile).string();
    Config cfg;
    cfg.load(configPath);
    int sceneIndex = std::clamp(cfg.scene, 0, kSceneCount - 1);

    // OpenCV would otherwise spread every YuNet call over all cores, starving the render and capture threads.
    cv::setNumThreads(std::clamp(int(std::thread::hardware_concurrency()) / 2, 2, kMaxOpenCvThreads));

    // ---- camera (640x480) ----
    cv::VideoCapture capture;
    cv::Mat frame;
    const int cameraIndex = argc > 1 ? std::atoi(argv[1]) : 0;
    if (!openCamera(cameraIndex, capture, frame)) { std::cerr << "Could not open camera " << cameraIndex << "\n"; return 1; }

    // ---- window ----
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(kWinW, kWinH, "Tesseract head-tracked parallax");
    SetExitKey(KEY_NULL);
    SetTargetFPS(std::max(60, GetMonitorRefreshRate(GetCurrentMonitor())));

    SceneShader scene;
    scene.load();
    SceneTextures textures;
    textures.load();

    ScreenDims dims = ResolveScreen(cfg, GetCurrentMonitor());
    Shared shared;
    PublishParams(shared, cfg, dims);

    FrameSource source;
    std::thread grabber(grabWorker, std::ref(capture), std::ref(source), std::cref(shared.quit));
    std::thread worker(trackingWorker, std::ref(source), yunetModel.string(), std::cref(predictor), std::ref(shared));

    Vector3 eyeWorld{0, kScreenCenterY, kScreenPlaneZ + 600.0f * float(kScreenHeightUnits / dims.hMm)};
    Vector3 targetPos = eyeWorld;
    // True geometry needs to know where the webcam is; until the photo setup is done, recenter on the user instead
    // (otherwise the view looks like it is peering down). P toggles either way.
    bool physicalAlignment = cfg.photoDone;
    bool prediction = true;
    bool showHud = true;
    bool fullscreen = false;
    WindowedState windowed;

    enum class Ui { View, Menu, Photo, Fov, Verify };
    Ui ui = Ui::View;
    NumberField field;
    bool fovFieldDiagonal = true;
    PhotoState photo;
    PhotoResult photoRes;
    double verifyMm = 100;

    std::string toast = "Sit naturally for a second to center.  [ ] strength | 1-5 scenes | C setup | V pen-tip test";
    double toastUntil = GetTime() + 9;
    auto showToast = [&](const std::string& m, double secs = 7) { toast = m; toastUntil = GetTime() + secs; };
    auto saveConfig = [&] {
        cfg.scene = sceneIndex;
        if (!cfg.save(configPath)) showToast("Could not write " + configPath);
    };
    auto fovLabel = [&]() {
        if (!(cfg.fovDeg > 10)) return cv::format("%.1f deg horizontal (default; not entered)", kDefaultHFovDeg);
        return cfg.fovDiagonal ? cv::format("%.1f deg diagonal = %.1f deg horizontal", cfg.fovDeg, cfg.hfovDeg())
                               : cv::format("%.1f deg horizontal", cfg.fovDeg);
    };
    const char* kEyeNames[] = {"both (midpoint)", "LEFT eye", "RIGHT eye"};

    bool running = true;
    while (running && !WindowShouldClose() && !shared.quit) {
        if (IsFileDropped()) {   // dropping a photo anywhere jumps straight to the photo setup
            FilePathList dropped = LoadDroppedFiles();
            if (dropped.count > 0) {
                std::string err;
                if (LoadPhoto(dropped.paths[0], photo, err)) ui = Ui::Photo;
                else showToast(err);
            }
            UnloadDroppedFiles(dropped);
        }
        if (IsKeyPressed(KEY_F11)) { fullscreen = !fullscreen; SetBorderlessFullscreen(fullscreen, windowed); }

        Snapshot snap;
        { std::lock_guard<std::mutex> lk(shared.m); snap = shared.snap; }

        // ---- geometry: physical screen -> world units ----
        const int   mon = GetCurrentMonitor();
        const ScreenDims newDims = ResolveScreen(cfg, mon);
        if (std::abs(newDims.hMm - dims.hMm) > 0.5 || std::abs(newDims.wMm - dims.wMm) > 0.5) {
            dims = newDims;
            PublishParams(shared, cfg, dims);
        }
        const int   sw = GetScreenWidth(), sh = std::max(1, GetScreenHeight());
        const float monWpx = float(std::max(1, GetMonitorWidth(mon)));
        const float monHpx = float(std::max(1, GetMonitorHeight(mon)));
        const double monAspect = double(monWpx) / monHpx;
        const float mmPerPxX = float(dims.wMm / monWpx), mmPerPxY = float(dims.hMm / monHpx);
        const float physHeightMm = mmPerPxY * sh, physWidthMm = mmPerPxX * sw;
        const float unitsPerMm = kScreenHeightUnits / physHeightMm;
        const float screenWidth = physWidthMm * unitsPerMm;
        const float L = -screenWidth / 2, Rr = screenWidth / 2;
        const float B = kScreenCenterY - screenHeight / 2, T = kScreenCenterY + screenHeight / 2;
        const Vector2 wp = GetWindowPosition(), mp = GetMonitorPosition(mon);
        const float winOffXMm =  ((wp.x - mp.x) + sw * 0.5f - monWpx * 0.5f) * mmPerPxX;
        const float winOffYMm = -((wp.y - mp.y) + sh * 0.5f - monHpx * 0.5f) * mmPerPxY;

        // ---- input per screen ----
        switch (ui) {
        case Ui::View:
            if (IsKeyPressed(KEY_ESCAPE)) running = false;
            if (IsKeyPressed(KEY_C)) ui = Ui::Menu;
            if (IsKeyPressed(KEY_V)) ui = Ui::Verify;
            if (IsKeyPressed(KEY_H)) showHud = !showHud;
            if (IsKeyPressed(KEY_R)) shared.recalibrate = true;
            if (IsKeyPressed(KEY_P)) {
                physicalAlignment = !physicalAlignment;
                showToast(physicalAlignment ? (cfg.photoDone ? "True geometry (from the screen photo)"
                                                             : "True geometry with a GUESSED webcam position: do the screen photo (C)")
                                            : "Recentered on your sitting position (R to recenter)", 4);
            }
            if (IsKeyPressed(KEY_L)) prediction = !prediction;
            if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET)) {
                cfg.strength = std::clamp(cfg.strength + (IsKeyPressed(KEY_LEFT_BRACKET) ? -0.05 : 0.05), 0.1, 1.5);
                saveConfig();
                showToast(cv::format("Parallax strength %.2f (1.00 = physically exact)", cfg.strength), 3);
            }
            for (int k = 0; k < kSceneCount; ++k)
                if (IsKeyPressed(KEY_ONE + k)) { sceneIndex = k; saveConfig(); showToast(kScenes[k].name, 3); }
            if (IsKeyPressed(KEY_TAB)) { sceneIndex = (sceneIndex + 1) % kSceneCount; saveConfig(); showToast(kScenes[sceneIndex].name, 3); }
            break;
        case Ui::Menu:
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_C)) ui = Ui::View;
            if (IsKeyPressed(KEY_ONE)) { ui = Ui::Photo; if (photo.loaded) { photo.stage = 0; photo.pts.clear(); } }
            if (IsKeyPressed(KEY_TWO)) {
                ui = Ui::Fov;
                fovFieldDiagonal = cfg.fovDeg > 10 ? cfg.fovDiagonal : true;
                // Pre-fill with the current setting (the default 70 deg horizontal is ~82 deg diagonal at 4:3).
                field.set(cfg.fovDeg > 10 ? cfg.fovDeg
                                          : 2.0 * std::atan(std::tan(kDefaultHFovDeg * CV_PI / 360.0) * std::hypot(double(kCamW), double(kCamH)) / kCamW) * 180.0 / CV_PI,
                          1);
            }
            if (IsKeyPressed(KEY_V)) ui = Ui::Verify;
            break;
        case Ui::Photo: {
            if (!photo.loaded) { if (IsKeyPressed(KEY_ESCAPE)) ui = Ui::Menu; break; }
            const float top = 150, pad = 20;
            const float fit = std::min((sw - 2 * pad) / float(photo.tex.width), (sh - top - pad) / float(photo.tex.height));
            const Vector2 org{(sw - photo.tex.width * fit) * 0.5f, top + (sh - top - pad - photo.tex.height * fit) * 0.5f};
            if (photo.stage == 0) {
                const Vector2 m = GetMousePosition();
                const Vector2 ip{(m.x - org.x) / fit, (m.y - org.y) / fit};
                const bool inside = ip.x >= 0 && ip.y >= 0 && ip.x < photo.tex.width && ip.y < photo.tex.height;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inside && photo.pts.size() < 5) photo.pts.push_back(ip);
                if ((IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_BACKSPACE)) && !photo.pts.empty())
                    photo.pts.pop_back();
                if (photo.pts.size() == 5) { photo.stage = 1; field.set(cfg.screenDiagIn > 0 ? cfg.screenDiagIn : 27.0, 1); }
                if (IsKeyPressed(KEY_ESCAPE)) ui = Ui::Menu;
            } else if (photo.stage == 1) {
                field.update();
                if (EnterPressed() && field.value() > 5 && field.value() < 200) {
                    photoRes = ComputePhoto(photo.pts, field.value(), monAspect);
                    photo.stage = 2;
                }
                if (IsKeyPressed(KEY_ESCAPE)) { photo.pts.pop_back(); photo.stage = 0; }
            } else {
                if (EnterPressed()) {
                    cfg.screenDiagIn = field.value();
                    cfg.screenWmm = photoRes.wMm; cfg.screenHmm = photoRes.hMm;
                    cfg.camOffXmm = photoRes.camX; cfg.camOffYmm = photoRes.camY;
                    cfg.photoDone = true;
                    saveConfig();
                    dims = ResolveScreen(cfg, mon);
                    PublishParams(shared, cfg, dims);
                    physicalAlignment = true;
                    showToast(cv::format("Saved: screen %.0f x %.0f mm, webcam at X %.0f, Y %.0f mm from center. Using true geometry.",
                                         cfg.screenWmm, cfg.screenHmm, cfg.camOffXmm, cfg.camOffYmm));
                    ui = Ui::Menu;
                }
                if (IsKeyPressed(KEY_ESCAPE)) { photo.pts.clear(); photo.stage = 0; }
            }
            break;
        }
        case Ui::Fov:
            field.update();
            if (IsKeyPressed(KEY_TAB)) fovFieldDiagonal = !fovFieldDiagonal;
            if (EnterPressed() && field.value() > 20 && field.value() < 160) {
                cfg.fovDeg = field.value();
                cfg.fovDiagonal = fovFieldDiagonal;
                saveConfig();
                PublishParams(shared, cfg, dims);
                showToast("Webcam field of view: " + fovLabel());
                ui = Ui::Menu;
            }
            if (IsKeyPressed(KEY_ESCAPE)) ui = Ui::Menu;
            break;
        case Ui::Verify:
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_V)) ui = Ui::View;
            if (IsKeyPressed(KEY_UP))   verifyMm = std::min(300.0, verifyMm + 10);
            if (IsKeyPressed(KEY_DOWN)) verifyMm = std::max(20.0, verifyMm - 10);
            if (IsKeyPressed(KEY_E)) shared.eyeSelect = (shared.eyeSelect.load() + 1) % 3;
            if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET)) {   // fine-tune the field of view
                if (!(cfg.fovDeg > 10)) { cfg.fovDeg = kDefaultHFovDeg; cfg.fovDiagonal = false; }
                cfg.fovDeg = std::clamp(cfg.fovDeg + (IsKeyPressed(KEY_LEFT_BRACKET) ? -0.5 : 0.5), 20.0, 160.0);
                PublishParams(shared, cfg, dims);
            }
            if (IsKeyPressed(KEY_S)) { saveConfig(); showToast("Saved webcam field of view: " + fovLabel()); }
            break;
        }

        // ---- eye position -> virtual camera ----
        // True geometry: eye relative to the center of the drawn area. Recentered: relative to where you sat
        // during the first second (R). Strength scales the sideways/vertical motion; the pen-tip test uses 1.0.
        const bool verify = ui == Ui::Verify;
        const bool absolute = verify || physicalAlignment;
        const double strength = verify ? 1.0 : cfg.strength;
        double predictS = 0.0;
        if (snap.valid && (absolute || snap.calibrated)) {
            if (prediction) {
                const double speed = cv::norm(snap.vel);
                const double g = std::clamp((speed - kPredictMinSpeed) / (kPredictFullSpeed - kPredictMinSpeed), 0.0, 1.0);
                predictS = g * g * (3 - 2 * g) *
                           std::clamp(Seconds(Clock::now() - snap.stamp) + kExtraLatencyS + 1.0 / kRenderSmoothRate, 0.0,
                                      kMaxExtrapolationS);
            }
            const cv::Vec3d eye = snap.pos + snap.vel * predictS;
            double xMm, yMm;
            if (absolute) { xMm = (eye[0] - winOffXMm) * strength; yMm = (eye[1] - winOffYMm) * strength; }
            else          { xMm = (eye[0] - snap.reference[0]) * strength; yMm = (eye[1] - snap.reference[1]) * strength; }
            const double zMm = std::max(eye[2], 120.0);
            targetPos = {float(xMm * unitsPerMm), kScreenCenterY + float(yMm * unitsPerMm), kScreenPlaneZ + float(zMm * unitsPerMm)};
        }
        const float k = 1.0f - std::exp(-GetFrameTime() * kRenderSmoothRate);
        eyeWorld = Vector3Add(eyeWorld, Vector3Scale(Vector3Subtract(targetPos, eyeWorld), k));
        const float screenDist = std::max(0.5f, eyeWorld.z - kScreenPlaneZ);

        // ---- draw ----
        BeginDrawing();
        if (ui == Ui::Photo) {
            if (!photo.loaded) {
                ClearBackground(WHITE);
                DrawRectangleLinesEx({0, 0, float(sw), float(sh)}, 6, BLACK);
                DrawLinesAt({"SETUP: SCREEN PHOTO",
                             "",
                             "1. Make this window fullscreen (F11) so the whole screen is white.",
                             "2. Take a photo of the monitor from straight in front, about 1-2 m away.",
                             "   The whole screen and the webcam must be visible. 2x zoom on the phone helps.",
                             "3. Copy the photo (JPG or PNG) to this computer.",
                             "4. Drag and drop it onto this window.",
                             "",
                             "ESC back"}, 40, 40, 22, BLACK);
            } else {
                ClearBackground(Color{25, 25, 30, 255});
                const float top = 150, pad = 20;
                const float fit = std::min((sw - 2 * pad) / float(photo.tex.width), (sh - top - pad) / float(photo.tex.height));
                const Vector2 org{(sw - photo.tex.width * fit) * 0.5f, top + (sh - top - pad - photo.tex.height * fit) * 0.5f};
                DrawTextureEx(photo.tex, org, 0.0f, fit, WHITE);
                auto toScreen = [&](Vector2 p) { return Vector2{org.x + p.x * fit, org.y + p.y * fit}; };
                const char* names[] = {"1 top-left", "2 top-right", "3 bottom-right", "4 bottom-left", "5 webcam lens"};
                for (size_t i = 0; i < photo.pts.size(); ++i) {
                    const Vector2 s = toScreen(photo.pts[i]);
                    if (i < 4 && i > 0) DrawLineEx(toScreen(photo.pts[i - 1]), s, 2, YELLOW);
                    if (i == 3) DrawLineEx(s, toScreen(photo.pts[0]), 2, YELLOW);
                    const Color c = i == 4 ? RED : YELLOW;
                    DrawCircleLines(int(s.x), int(s.y), 7, c);
                    DrawLine(int(s.x) - 12, int(s.y), int(s.x) + 12, int(s.y), c);
                    DrawLine(int(s.x), int(s.y) - 12, int(s.x), int(s.y) + 12, c);
                    DrawText(names[i], int(s.x) + 10, int(s.y) + 8, 18, c);
                }
                DrawPanel(0, 0, sw, int(top) - 10);
                if (photo.stage == 0) {
                    DrawLinesAt({cv::format("SETUP: click point %d of 5:  %s", int(photo.pts.size()) + 1,
                                            names[std::min<size_t>(photo.pts.size(), 4)]),
                                 "Corners = the corners of the LIT picture area (not the bezel), as you face the screen.",
                                 "Webcam = the center of the lens.   Right click / Backspace = undo.   ESC = back",
                                 "Photo: " + photo.name + "   (drop another photo to replace it)"}, 16, 12, 20, RAYWHITE);
                    const Vector2 m = GetMousePosition();
                    const Vector2 ip{(m.x - org.x) / fit, (m.y - org.y) / fit};
                    if (ip.x >= 0 && ip.y >= 0 && ip.x < photo.tex.width && ip.y < photo.tex.height) {
                        const float box = 220, zoom = 5.0f;
                        const float srcHalf = box / (fit * zoom) * 0.5f;
                        Vector2 at{m.x + 24, m.y + 24};
                        if (at.x + box > sw) at.x = m.x - 24 - box;
                        if (at.y + box > sh) at.y = m.y - 24 - box;
                        DrawTexturePro(photo.tex, {ip.x - srcHalf, ip.y - srcHalf, srcHalf * 2, srcHalf * 2},
                                       {at.x, at.y, box, box}, {0, 0}, 0.0f, WHITE);
                        DrawRectangleLinesEx({at.x, at.y, box, box}, 2, RAYWHITE);
                        DrawLine(int(at.x + box / 2), int(at.y), int(at.x + box / 2), int(at.y + box), RED);
                        DrawLine(int(at.x), int(at.y + box / 2), int(at.x + box), int(at.y + box / 2), RED);
                    }
                } else if (photo.stage == 1) {
                    DrawLinesAt({"SETUP: monitor diagonal size in inches (the advertised size, e.g. 27):",
                                 "> " + field.text + "_",
                                 cv::format("Aspect ratio comes from this monitor's resolution: %dx%d (%.3f:1).",
                                            int(monWpx), int(monHpx), monAspect),
                                 "ENTER = compute    ESC = redo the last click"}, 16, 12, 20, RAYWHITE);
                } else {
                    std::vector<std::string> lines = {
                        cv::format("Screen: %.0f x %.0f mm.  Webcam lens: X %+.0f mm, Y %+.0f mm from the screen center.",
                                   photoRes.wMm, photoRes.hMm, photoRes.camX, photoRes.camY),
                        cv::format("(Webcam %.0f mm above the top edge.)  ENTER = save    ESC = redo the clicks",
                                   photoRes.camY - photoRes.hMm / 2)};
                    for (const auto& w : photoRes.warnings) lines.push_back("Warning: " + w);
                    DrawLinesAt(lines, 16, 12, 20, photoRes.warnings.empty() ? GREEN : ORANGE);
                }
            }
        } else {
            // ---- 3D scene ----
            const SceneDef& sd = verify ? kScenes[4] : kScenes[sceneIndex];
            const Vector3 light = Vector3Normalize(sd.light);
            if (scene.ok) {
                const float fogNear = screenDist + sd.fogStart, fogFar = screenDist + sd.fogEnd;
                SetShaderValue(scene.sh, scene.viewPos, &eyeWorld, SHADER_UNIFORM_VEC3);
                SetShaderValue(scene.sh, scene.lightDir, &light, SHADER_UNIFORM_VEC3);
                SetShaderValue(scene.sh, scene.fogColor, &sd.fog, SHADER_UNIFORM_VEC3);
                SetShaderValue(scene.sh, scene.fogNear, &fogNear, SHADER_UNIFORM_FLOAT);
                SetShaderValue(scene.sh, scene.fogFar, &fogFar, SHADER_UNIFORM_FLOAT);
                SetShaderValue(scene.sh, scene.fogMax, &sd.fogMax, SHADER_UNIFORM_FLOAT);
            }
            Camera3D camera{};
            camera.position = eyeWorld;
            camera.target = {eyeWorld.x, eyeWorld.y, eyeWorld.z - 1.0f};   // always facing straight into the screen
            camera.up = {0, 1, 0};
            camera.fovy = 45.0f;                                            // unused: projection replaced below
            camera.projection = CAMERA_PERSPECTIVE;
            ClearBackground(sd.sky);
            BeginMode3D(camera);
            ApplyOffAxisProjection(eyeWorld, screenWidth, screenHeight, screenDist);
            if (scene.ok) BeginShaderMode(scene.sh);
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();   // the shader orients normals toward the viewer, so both faces shade correctly

            SceneCtx ctx{L, Rr, B, T, kScreenPlaneZ, GetTime(), &scene, &textures, light};
            if (verify) {
                Lit(ctx, true);
                rlDrawRenderBatchActive();
                DrawBoxRoom(L, Rr, B, T, kScreenPlaneZ, kScreenPlaneZ - 25.0f, Color{196, 198, 206, 255}, Color{222, 224, 232, 255},
                            Color{236, 237, 242, 255}, Color{210, 212, 220, 255});
                const Vector3 tip{0, kScreenCenterY, kScreenPlaneZ + float(verifyMm) * unitsPerMm};
                DrawSphereEx(tip, 2.5f * unitsPerMm, 16, 16, RED);
                Lit(ctx, false);
                DrawGrid(L, Rr, B, T, kScreenPlaneZ, kScreenPlaneZ - 25.0f, 2.5f, 1.25f, Color{118, 122, 136, 255});
                const float cr = 15.0f * unitsPerMm, z = kScreenPlaneZ + 0.005f;
                DrawLine3D({-cr, kScreenCenterY, z}, {cr, kScreenCenterY, z}, BLACK);
                DrawLine3D({0, kScreenCenterY - cr, z}, {0, kScreenCenterY + cr, z}, BLACK);
            } else {
                sd.draw(ctx);
            }
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            if (scene.ok) EndShaderMode();
            EndMode3D();

            const Vector3 eyeMm = Vector3Scale(Vector3Subtract(eyeWorld, {0, kScreenCenterY, kScreenPlaneZ}), 1.0f / unitsPerMm);
            if (ui == Ui::View && showHud) {
                DrawRectangle(0, 0, std::min(sw, 900), 150, Fade(BLACK, 0.5f));
                DrawFPS(10, 8);
                DrawText(cv::format("Scene %d/%d: %s   (1-%d / TAB)", sceneIndex + 1, kSceneCount, kScenes[sceneIndex].name,
                                    kSceneCount).c_str(), 110, 10, 18, RAYWHITE);
                DrawText("[ ] strength | P geometry | R recenter | L prediction | C setup | V pen-tip test | H hud | F11 | ESC",
                         10, 34, 16, RAYWHITE);
                DrawText(cv::format("Strength %.2f | %s | prediction %s (%.0f ms) | latency %.0f ms",
                                    cfg.strength, physicalAlignment ? "true geometry" : "recentered",
                                    prediction ? "on" : "off", predictS * 1000.0, snap.procMs).c_str(), 10, 56, 16, RAYWHITE);
                DrawText(cv::format("Eye vs window center (mm): X %.0f  Y %.0f  Z %.0f   (Z should match a tape measure)",
                                    snap.valid ? snap.pos[0] - winOffXMm : 0.0, snap.valid ? snap.pos[1] - winOffYMm : 0.0,
                                    snap.valid ? snap.pos[2] : 0.0).c_str(), 10, 78, 16, RAYWHITE);
                DrawText(cv::format("Screen %.0f x %.0f mm (%s) | webcam FOV %s | camera %dx%d @ %.0f fps | %.1f ms/frame",
                                    dims.wMm, dims.hMm, dims.source.c_str(), fovLabel().c_str(), snap.camW, snap.camH,
                                    snap.camFps, snap.workMs).c_str(), 10, 100, 16, RAYWHITE);
                DrawText(cv::format("%s | YuNet %.2f | reprojection %.1f px", snap.status.c_str(), snap.faceScore,
                                    snap.reprojErr).c_str(), 10, 122, 16, snap.tracking ? GREEN : ORANGE);
            } else if (ui == Ui::Menu) {
                DrawPanel(30, 30, std::min(sw - 60, 940), 260);
                DrawLinesAt({"SETUP  - press a number",
                             "",
                             "1  Screen photo (exact screen size + webcam position)   " +
                                 (cfg.photoDone ? cv::format("[done: %.1f in, webcam X %+.0f Y %+.0f mm]", cfg.screenDiagIn,
                                                             cfg.camOffXmm, cfg.camOffYmm)
                                                : "[not done: using " + dims.source + "]"),
                             "2  Webcam field of view   [" + fovLabel() + "]",
                             "",
                             "V  Pen-tip test (checks the setup)",
                             "ESC / C = back",
                             "Saved to: " + configPath}, 50, 46, 20, RAYWHITE);
            } else if (ui == Ui::Fov) {
                // Distance scales with the focal length: preview what the entered value would read.
                const double entered = field.value();
                const double hfovNew = entered > 20 && entered < 160
                                           ? (fovFieldDiagonal ? DiagonalToHorizontalFov(entered) : entered) : 0.0;
                const double hfovCur = cfg.hfovDeg();
                const double zNew = hfovNew > 0 ? snap.rawZ * std::tan(hfovCur * CV_PI / 360.0) / std::tan(hfovNew * CV_PI / 360.0) : 0.0;
                DrawPanel(30, 30, std::min(sw - 60, 940), 340);
                DrawLinesAt({"SETUP: WEBCAM FIELD OF VIEW",
                             "Find it on the webcam's spec sheet (usually the DIAGONAL, e.g. Logitech C920 = 78).",
                             std::string("Type it in degrees. TAB switches between diagonal and horizontal. Now: ") +
                                 (fovFieldDiagonal ? "DIAGONAL" : "HORIZONTAL"),
                             "",
                             "> " + field.text + " deg_" +
                                 (hfovNew > 0 && fovFieldDiagonal ? cv::format("   (= %.1f deg horizontal)", hfovNew) : std::string()),
                             "",
                             "Note: at 640x480 many webcams crop the sensor, so the real FOV can be smaller than the spec.",
                             snap.tracking ? cv::format("Your distance would read %.0f mm with this value (now %.0f mm). Check it with a tape measure:",
                                                        zNew, snap.rawZ)
                                           : std::string("Face not tracked (sit in front of the webcam to see the distance)."),
                             "too SHORT -> lower the FOV, too LONG -> raise it.",
                             "ENTER = save    ESC = back"}, 50, 46, 20, RAYWHITE);
            } else if (ui == Ui::Verify) {
                DrawPanel(10, 10, std::min(sw - 20, 960), 236);
                DrawLinesAt({"PEN-TIP TEST  (fullscreen with F11 works best; true geometry at strength 1.00)",
                             cv::format("Hold a pen tip %.0f mm straight out from the black cross (UP/DOWN to change).", verifyMm),
                             cv::format("Close one eye, press E until it says that eye. Now: %s", kEyeNames[shared.eyeSelect.load()]),
                             "Move your head slowly. The red dot should stay glued to the pen tip.",
                             "  Dot slides the SAME way as your head -> raise the FOV.   Opposite way -> lower it.",
                             "  Steady up/down or left/right offset -> webcam position (redo the screen photo).",
                             "[ ] fine-tune FOV (now " + fovLabel() + ")   S = save   ESC = done",
                             cv::format("Eye vs window center (mm): X %.0f  Y %.0f  Z %.0f", eyeMm.x, eyeMm.y, eyeMm.z)},
                            24, 22, 19, RAYWHITE);
            }
        }
        if (GetTime() < toastUntil && !toast.empty()) {
            const int w = std::min(sw - 20, MeasureText(toast.c_str(), 18) + 24);
            DrawRectangle(10, sh - 44, w, 34, Fade(BLACK, 0.8f));
            DrawText(toast.c_str(), 22, sh - 36, 18, YELLOW);
        }
        EndDrawing();
    }

    shared.quit = true;
    source.ready.notify_all();
    worker.join();
    grabber.join();
    capture.release();
    if (photo.loaded) UnloadTexture(photo.tex);
    textures.unload();
    if (scene.ok) UnloadShader(scene.sh);
    CloseWindow();
    return 0;
}
