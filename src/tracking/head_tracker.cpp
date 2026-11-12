#include "tracking/head_tracker.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <dlib/image_processing.h>
#include <dlib/opencv.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace tess {
namespace {

// ---------------------------------------------------------------- tuning ----
constexpr int kCalibrationFrames = 30;      // measurements averaged for the recentered mode's reference (~1 s)
constexpr int kMaxOpenCvThreads = 4;        // leave the other cores for rendering and capture

// Face detection.
constexpr int   kDetectWidth = 640;         // YuNet input width (frames wider than this are downscaled)
constexpr float kYuNetScoreThreshold = 0.7f;
constexpr float kYuNetNmsThreshold   = 0.3f;
constexpr int   kYuNetTopK           = 50;

constexpr double kTrackGapResetS = 0.5;

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

// ------------------------------------------------------- shared w/ render ---
struct Shared {
    std::mutex m;
    Snapshot snap;
    std::atomic<bool> quit{false};
    std::atomic<bool> recalibrate{false};
    std::mutex pm;
    TrackerParams params;
    std::atomic<int>  paramsVersion{0};
    std::atomic<int>  debugView{int(DebugView::Window)};
    std::mutex dm;
    cv::Mat debugRgba;
    std::atomic<std::uint64_t> debugSeq{0};
};

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
    double tiltRad = 0;
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
    m.tiltRad = p.tiltDeg * CV_PI / 180.0;
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
    Tracker() {
        for (int i = 0; i < 2; ++i) { filt[i].minCutoff = kXYMinCutoff; filt[i].beta = kXYBeta; }
        filt[2].minCutoff = kZMinCutoff; filt[2].beta = kZBeta;
        snap.status = "Starting camera";
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
    const cv::Point2d eyePx = (leftEye + rightEye) * 0.5;
    dbg.circle(eyePx, 4, cv::Scalar(0, 0, 255), cv::FILLED);
    const cv::Vec3d camEye((eyePx.x - cx) / f * Z, (eyePx.y - cy) / f * Z, Z);
    if (!(std::isfinite(camEye[0]) && std::isfinite(camEye[1]) && std::isfinite(camEye[2]))) {
        s.status = "Invalid eye position"; t.havePose = false; return false;
    }
    s.rawZ = Z;

    // Webcam tilt: a webcam pitched down by tilt sees the world rotated; undo it so "forward" means straight out
    // of the screen. Camera space is x right, y down, z forward.
    const double cs = std::cos(cam.tiltRad), sn = std::sin(cam.tiltRad);
    const cv::Vec3d level(camEye[0], camEye[1] * cs + camEye[2] * sn, -camEye[1] * sn + camEye[2] * cs);

    // Webcam space (x image-right, y image-down, z toward you) -> screen space (+X your right, +Y up, +Z toward you).
    const cv::Vec3d scr(-level[0] + cam.offset[0], -level[1] + cam.offset[1], level[2] + cam.offset[2]);

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
    bool windowOpen = false;

    cv::Ptr<cv::FaceDetectorYN> yunet;
    try {
        yunet = cv::FaceDetectorYN::create(yunetModel, "", cv::Size(kCamW, kCamH), kYuNetScoreThreshold, kYuNetNmsThreshold,
                                           kYuNetTopK);
    } catch (const cv::Exception& e) {
        std::cerr << "YuNet load failed: " << e.what() << "\n";
        std::lock_guard<std::mutex> lk(shared.m);
        shared.snap.status = std::string("YuNet load failed: ") + e.what();
        return;
    }

    Tracker t;
    CamModel cam;
    TrackerParams params;
    int paramsSeen = -1;
    cv::Size camSize;
    cv::Mat frame, rgba;
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
        const DebugView view = DebugView(shared.debugView.load());
        if (view == DebugView::Window && !windowOpen) {
            cv::namedWindow(kWin, cv::WINDOW_NORMAL);
            cv::resizeWindow(kWin, 760, 570);
            windowOpen = true;
        } else if (view != DebugView::Window && windowOpen) {
            cv::destroyWindow(kWin);
            windowOpen = false;
        }

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

            if (view != DebugView::Off) {
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
                put(cv::format("Webcam FOV %.1f deg horizontal | tilt %.0f deg", params.hfovDeg, params.tiltDeg), 96);
                if (view == DebugView::Window) {
                    cv::imshow(kWin, dbg.img);
                } else {
                    cv::cvtColor(dbg.img, rgba, cv::COLOR_BGR2RGBA);
                    std::lock_guard<std::mutex> lk(shared.dm);
                    rgba.copyTo(shared.debugRgba);
                    ++shared.debugSeq;
                }
            }
        } else {
            t.snap.tracking = false;
            t.snap.status = "Waiting for camera frames";
            std::lock_guard<std::mutex> lk(shared.m);
            shared.snap = t.snap;
        }

        if (windowOpen) {
            const int key = cv::waitKey(1);   // keeps the diagnostics window responsive
            if (key == 'r' || key == 'R') t.reset();
        }
    }
    if (windowOpen) cv::destroyWindow(kWin);
}

} // namespace

// ------------------------------------------------------------ HeadTracker ---
TrackerParams MakeTrackerParams(const Config& c, const ScreenDims& dims) {
    TrackerParams p;
    p.hfovDeg = c.hfovDeg();
    p.offset = c.photoDone ? cv::Vec3d(c.camOffXmm, c.camOffYmm, c.camOffZmm)
                           : cv::Vec3d(0, dims.hMm / 2 + kWebcamAboveTopEdgeMm, 0);
    p.tiltDeg = c.camTiltDeg;
    return p;
}

struct HeadTracker::Impl {
    cv::VideoCapture capture;
    dlib::shape_predictor predictor;
    std::string yunetPath;
    bool modelsLoaded = false;
    FrameSource source;
    Shared shared;
    std::thread grabber, worker;
    std::atomic<bool> running{false};
};

HeadTracker::HeadTracker() : impl_(std::make_unique<Impl>()) {}
HeadTracker::~HeadTracker() { stopCamera(); }

bool HeadTracker::loadModels(const std::filesystem::path& landmarkModel, const std::filesystem::path& yunetModel,
                             std::string& error) {
    Impl& im = *impl_;
    if (landmarkModel.empty() || !std::filesystem::exists(landmarkModel)) {
        error = "Missing dlib predictor: custom/shape_predictor_68_face_landmarks.dat";
        return false;
    }
    try { dlib::deserialize(landmarkModel.string()) >> im.predictor; }
    catch (const std::exception& e) { error = std::string("Could not load the dlib predictor: ") + e.what(); return false; }
    if (yunetModel.empty() || !std::filesystem::exists(yunetModel)) {
        error = "Missing YuNet model: custom/face_detection_yunet_2023mar.onnx";
        return false;
    }
    im.yunetPath = yunetModel.string();
    // OpenCV would otherwise spread every YuNet call over all cores, starving the render and capture threads.
    cv::setNumThreads(std::clamp(int(std::thread::hardware_concurrency()) / 2, 2, kMaxOpenCvThreads));
    im.modelsLoaded = true;
    return true;
}

bool HeadTracker::startCamera(int cameraIndex, std::string& error) {
    Impl& im = *impl_;
    if (im.running) return true;
    if (!im.modelsLoaded) { error = "Models are not loaded"; return false; }
    { std::lock_guard<std::mutex> lk(im.shared.m); im.shared.snap = Snapshot{}; im.shared.snap.status = "Starting camera"; }
    cv::Mat frame;
    if (!openCamera(cameraIndex, im.capture, frame)) {
        error = "Could not open camera " + std::to_string(cameraIndex);
        std::lock_guard<std::mutex> lk(im.shared.m);
        im.shared.snap.status = error;
        return false;
    }
    im.shared.quit = false;
    im.grabber = std::thread(grabWorker, std::ref(im.capture), std::ref(im.source), std::cref(im.shared.quit));
    im.worker = std::thread(trackingWorker, std::ref(im.source), im.yunetPath, std::cref(im.predictor), std::ref(im.shared));
    im.running = true;
    return true;
}

void HeadTracker::stopCamera() {
    Impl& im = *impl_;
    if (!im.running) return;
    im.shared.quit = true;
    im.source.ready.notify_all();
    im.worker.join();
    im.grabber.join();
    im.capture.release();
    im.running = false;
    std::lock_guard<std::mutex> lk(im.shared.m);
    im.shared.snap = Snapshot{};
    im.shared.snap.status = "Camera off";
}

bool HeadTracker::cameraActive() const { return impl_->running; }

Snapshot HeadTracker::snapshot() const {
    std::lock_guard<std::mutex> lk(impl_->shared.m);
    return impl_->shared.snap;
}

void HeadTracker::setParams(const TrackerParams& p) {
    Shared& sh = impl_->shared;
    std::lock_guard<std::mutex> lk(sh.pm);
    sh.params = p;
    ++sh.paramsVersion;
}

void HeadTracker::recenter() { impl_->shared.recalibrate = true; }
void HeadTracker::setDebugView(DebugView view) { impl_->shared.debugView = int(view); }

bool HeadTracker::debugImage(cv::Mat& rgba, std::uint64_t& seq) const {
    const Shared& sh = impl_->shared;
    const std::uint64_t cur = sh.debugSeq.load();
    if (cur == seq) return false;
    std::lock_guard<std::mutex> lk(impl_->shared.dm);
    if (sh.debugRgba.empty()) return false;
    sh.debugRgba.copyTo(rgba);
    seq = cur;
    return true;
}

} // namespace tess
