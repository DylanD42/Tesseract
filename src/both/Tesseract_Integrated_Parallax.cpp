// Tesseract: head-tracked "window" parallax
// dlib (face landmarks) + OpenCV (solvePnP + separate diagnostics window) + raylib (off-axis projection)
// Link with raylib, OpenCV (core, calib3d, videoio, highgui, imgproc) and dlib.
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <dlib/image_processing.h>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/opencv.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

// ---------------------------------------------------------------- scene -----
struct ViewerPose { Vector3 position; Vector3 target; Vector3 up; };

enum class ShapeType { Cube, Sphere, Cylinder, Cone, Capsule };

struct FloatingShape { ShapeType type; Vector3 position; float size; Color color; };

void DrawFloatingShape(const FloatingShape& s) {
    switch (s.type) {
        case ShapeType::Cube:     DrawCube(s.position, s.size, s.size, s.size, s.color); break;
        case ShapeType::Sphere:   DrawSphere(s.position, s.size * 0.5f, s.color); break;
        case ShapeType::Cylinder: DrawCylinder(s.position, s.size * 0.35f, s.size * 0.35f, s.size, 12, s.color); break;
        case ShapeType::Cone:     DrawCylinder(s.position, 0.0f, s.size * 0.45f, s.size, 12, s.color); break;
        case ShapeType::Capsule:
            DrawCapsule(Vector3{ s.position.x, s.position.y - s.size * 0.3f, s.position.z },
                        Vector3{ s.position.x, s.position.y + s.size * 0.3f, s.position.z },
                        s.size * 0.28f, 8, 8, s.color);
            break;
    }
}

Camera3D CreateCamera(const ViewerPose& v) {
    Camera3D c = {};
    c.position = v.position; c.target = v.target; c.up = v.up;
    c.fovy = 45.0f; c.projection = CAMERA_PERSPECTIVE;
    return c;
}

namespace {
// ---------------------------------------------------------------- config ----
constexpr int   kWinW = 1000, kWinH = 800;
constexpr int   kCalibrationFrames = 30;
constexpr float kScreenHeightUnits = 10.0f;  // world units spanned by the window height
constexpr float kScreenPlaneZ = 5.0f;        // the "glass" sits at this world Z
constexpr float kScreenCenterY = 1.0f;
// Physical size of your monitor. Measure the visible panel height with a ruler.
constexpr float kMonitorHeightMm = 300.0f;   // visible panel height
constexpr int   kMonitorHeightPx = 1080;     // panel vertical resolution (only used for the initial camera distance)
// Webcam position relative to the SCREEN CENTER (only used in physical-alignment mode, key P).
constexpr float kCameraOffsetXMm = 0.0f;     // + = camera right of center
constexpr float kCameraOffsetYMm = 165.0f;   // + = camera above center
constexpr double kCameraHFovDeg = 70.0;      // webcam horizontal FOV guess; tune if parallax feels too strong/weak
constexpr int   kDetectWidth = 320;          // face detector runs on a downscaled frame for speed

// Generic face model in mm, OpenCV camera convention (x right, y down, z away from camera).
// Origin = nose tip. Order matches kLandmarkIdx below.
const int kLandmarkIdx[] = {30, 33, 8, 36, 45, 39, 42, 48, 54};
const std::vector<cv::Point3d> kFaceModel = {
    {   0,   0,  0},   // 30 nose tip
    {   0,  16, 14},   // 33 base of nose
    {   0,  80, 25},   // 8  chin
    { -45, -35, 38},   // 36 outer eye corner (image left)
    {  45, -35, 38},   // 45 outer eye corner (image right)
    { -16, -35, 30},   // 39 inner eye corner (image left)
    {  16, -35, 30},   // 42 inner eye corner (image right)
    { -25,  35, 25},   // 48 mouth corner (image left)
    {  25,  35, 25}    // 54 mouth corner (image right)
};
// Midpoint between the eyeballs in the same face frame.
const cv::Point3d kEyeCenterInFaceMm(0.0, -35.0, 44.0);

// ------------------------------------------------------------- filtering ----
struct OneEuro {
    double minCutoff = 1.2, beta = 0.05, dCutoff = 1.0;
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
    void reset() { init = false; }
};

// ------------------------------------------------------- shared w/ render ---
struct Snapshot {
    bool valid = false;        // have a filtered eye position
    bool calibrated = false;
    bool tracking = false;     // this frame produced a measurement
    int collected = 0, faceCount = 0;
    double reprojErr = 0;
    cv::Vec3d eye{0, 0, 0};        // filtered eye position, camera space, mm
    cv::Vec3d reference{0, 0, 0};  // eye position captured during calibration
    std::string status = "Starting camera";
};

struct Shared {
    std::mutex m;
    Snapshot snap;
    std::atomic<bool> quit{false};
    std::atomic<bool> recalibrate{false};
};

struct Tracker {
    Snapshot snap;
    int collected = 0;
    cv::Vec3d sum{0, 0, 0};
    OneEuro filt[3];
    cv::Mat rvec, tvec, small;
    bool havePose = false;
    void reset() {
        collected = 0; sum = cv::Vec3d(0, 0, 0); havePose = false;
        for (auto& f : filt) f.reset();
        snap.valid = false; snap.calibrated = false; snap.collected = 0;
        snap.reference = cv::Vec3d(0, 0, 0); snap.status = "Recalibrating";
    }
};

// ---------------------------------------------------------------- camera ----
bool openCamera(int index, cv::VideoCapture& camera, cv::Mat& frame) {
    for (int backend : {(int)cv::CAP_DSHOW, (int)cv::CAP_MSMF, (int)cv::CAP_ANY}) {
        camera.release();
        if (!camera.open(index, backend)) continue;
        camera.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        camera.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (camera.read(frame) && !frame.empty()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    camera.release();
    return false;
}

cv::Mat approximateIntrinsics(cv::Size size) {
    const double focal = size.width / (2.0 * std::tan(kCameraHFovDeg * CV_PI / 360.0));
    return (cv::Mat_<double>(3, 3) << focal, 0, size.width * 0.5, 0, focal, size.height * 0.5, 0, 0, 1);
}

// -------------------------------------------------------------- tracking ----
// `frame` is never drawn on; all markers go to `debug`.
bool updateTracking(const cv::Mat& frame, cv::Mat& debug,
                    dlib::frontal_face_detector& detector, const dlib::shape_predictor& predictor,
                    const cv::Mat& K, const cv::Mat& dist, double dt, Tracker& t) {
    Snapshot& s = t.snap;
    s.tracking = false;

    const double scale = std::min(1.0, double(kDetectWidth) / frame.cols);
    cv::resize(frame, t.small, cv::Size(), scale, scale, cv::INTER_AREA);
    dlib::cv_image<dlib::bgr_pixel> smallImg(t.small);
    dlib::cv_image<dlib::bgr_pixel> fullImg(frame);

    const auto faces = detector(smallImg);
    s.faceCount = static_cast<int>(faces.size());
    if (faces.empty()) { s.status = "No face detected"; t.havePose = false; return false; }

    const dlib::rectangle best = *std::max_element(faces.begin(), faces.end(),
        [](const dlib::rectangle& a, const dlib::rectangle& b) { return a.area() < b.area(); });
    const dlib::rectangle face(std::lround(best.left() / scale), std::lround(best.top() / scale),
                               std::lround(best.right() / scale), std::lround(best.bottom() / scale));

    const auto lm = predictor(fullImg, face);   // landmarks on the clean full-res frame
    if (lm.num_parts() != 68) { s.status = "Invalid landmark count"; return false; }

    cv::rectangle(debug, cv::Point(face.left(), face.top()), cv::Point(face.right(), face.bottom()),
                  cv::Scalar(0, 255, 0), 2);
    for (unsigned long i = 0; i < lm.num_parts(); ++i)
        cv::circle(debug, cv::Point(lm.part(i).x(), lm.part(i).y()), 2, cv::Scalar(0, 255, 255), cv::FILLED);

    std::vector<cv::Point2d> img;
    for (int i : kLandmarkIdx) img.emplace_back(lm.part(i).x(), lm.part(i).y());
    const double eyeDistPx = std::max(1.0, cv::norm(img[3] - img[4]));

    const double f = K.at<double>(0, 0), cx = K.at<double>(0, 2), cy = K.at<double>(1, 2);
    if (!t.havePose) {  // seed the solver with a sane pose so it can't flip behind the camera
        const double z0 = f * 90.0 / eyeDistPx;  // ~90 mm between outer eye corners
        t.rvec = cv::Mat::zeros(3, 1, CV_64F);
        t.tvec = (cv::Mat_<double>(3, 1) << (img[0].x - cx) / f * z0, (img[0].y - cy) / f * z0, z0);
    }
    const bool ok = cv::solvePnP(kFaceModel, img, K, dist, t.rvec, t.tvec, true, cv::SOLVEPNP_ITERATIVE);
    const double tz = ok ? t.tvec.at<double>(2) : -1.0;
    if (!ok || !std::isfinite(tz) || tz < 150 || tz > 3000) {
        s.status = "solvePnP failed / bad depth"; t.havePose = false; return false;
    }

    std::vector<cv::Point2d> reproj;
    cv::projectPoints(kFaceModel, t.rvec, t.tvec, K, dist, reproj);
    double err = 0;
    for (size_t i = 0; i < img.size(); ++i) {
        err += cv::norm(img[i] - reproj[i]);
        cv::circle(debug, cv::Point(cvRound(reproj[i].x), cvRound(reproj[i].y)), 3, cv::Scalar(0, 0, 255), 1);
    }
    err /= img.size();
    s.reprojErr = err;
    cv::drawFrameAxes(debug, K, dist, t.rvec, t.tvec, 50.0f);
    // Loose sanity check relative to face size (a fixed pixel threshold breaks at different distances).
    if (!std::isfinite(err) || err > 0.30 * eyeDistPx) {
        s.status = "Poor PnP fit"; t.havePose = false; return false;
    }
    t.havePose = true;

    cv::Mat R; cv::Rodrigues(t.rvec, R);
    cv::Mat eyeLocal = (cv::Mat_<double>(3, 1) << kEyeCenterInFaceMm.x, kEyeCenterInFaceMm.y, kEyeCenterInFaceMm.z);
    cv::Mat e = R * eyeLocal + t.tvec;
    cv::Vec3d raw(e.at<double>(0), e.at<double>(1), e.at<double>(2));
    if (!(std::isfinite(raw[0]) && std::isfinite(raw[1]) && std::isfinite(raw[2]))) {
        s.status = "Invalid eye position"; t.havePose = false; return false;
    }

    for (int i = 0; i < 3; ++i) s.eye[i] = t.filt[i].filter(raw[i], dt);
    s.valid = true;

    if (t.collected < kCalibrationFrames) {
        t.sum += s.eye;
        ++t.collected;
        s.collected = t.collected;
        if (t.collected == kCalibrationFrames) {
            s.reference = t.sum / double(kCalibrationFrames);
            s.calibrated = true;
        }
    }
    s.tracking = true;
    s.status = !s.calibrated ? "Hold still: calibrating"
             : (err > 0.10 * eyeDistPx ? "Tracking (low confidence)" : "Tracking OK");
    return true;
}

// Camera + dlib + OpenCV window all live on this thread so raylib stays at 60 FPS.
void trackingWorker(cv::VideoCapture& capture, cv::Size camSize,
                    dlib::frontal_face_detector& detector, const dlib::shape_predictor& predictor,
                    Shared& shared) {
    const char* kWin = "Tesseract - Webcam Diagnostics";
    cv::namedWindow(kWin, cv::WINDOW_NORMAL);
    cv::resizeWindow(kWin, 760, 570);

    const cv::Mat K = approximateIntrinsics(camSize);
    const cv::Mat dist = cv::Mat::zeros(5, 1, CV_64F);
    Tracker t;
    cv::Mat frame;
    auto last = std::chrono::steady_clock::now();

    while (!shared.quit) {
        if (shared.recalibrate.exchange(false)) t.reset();

        cv::Mat debug;
        if (capture.read(frame) && !frame.empty() && frame.size() == camSize) {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::clamp(std::chrono::duration<double>(now - last).count(), 0.001, 0.2);
            last = now;

            debug = frame.clone();
            updateTracking(frame, debug, detector, predictor, K, dist, dt, t);
            cv::flip(debug, debug, 1);  // mirrored for readability only; tracking uses the raw frame

            const Snapshot& s = t.snap;
            auto put = [&](const std::string& msg, int y, cv::Scalar c = cv::Scalar(255, 255, 255)) {
                cv::putText(debug, msg, cv::Point(12, y), cv::FONT_HERSHEY_SIMPLEX, 0.52, c, 2, cv::LINE_AA);
            };
            put("Faces: " + std::to_string(s.faceCount) + " | " + s.status, 24,
                s.tracking ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 140, 255));
            put(cv::format("PnP reprojection: %.2f px", s.reprojErr), 48);
            if (s.valid)
                put(cv::format("Eye mm (filtered): X %.1f  Y %.1f  Z %.1f", s.eye[0], s.eye[1], s.eye[2]), 72);
            put(cv::format("Calibration: %d/%d", s.collected, kCalibrationFrames), 96);
            put("R = recenter | Q/Esc = exit", 120);
            cv::imshow(kWin, debug);
        } else {
            t.snap.tracking = false;
            t.snap.status = "Camera read failed or resolution changed";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        { std::lock_guard<std::mutex> lk(shared.m); shared.snap = t.snap; }

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

void ApplyOffAxisProjection(const ViewerPose& v, float screenW, float screenH,
                            float screenDist, float nearPlane, float farPlane) {
    const float left   = nearPlane * (-screenW * 0.5f - v.position.x) / screenDist;
    const float right  = nearPlane * ( screenW * 0.5f - v.position.x) / screenDist;
    const float bottom = nearPlane * (-screenH * 0.5f - (v.position.y - kScreenCenterY)) / screenDist;
    const float top    = nearPlane * ( screenH * 0.5f - (v.position.y - kScreenCenterY)) / screenDist;
    rlSetMatrixProjection(MatrixFrustum(left, right, bottom, top, nearPlane, farPlane));
}
} // namespace

// ------------------------------------------------------------------ main ----
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const float screenHeight = kScreenHeightUnits;
    const float nearPlane = 0.05f;

    fs::path model = fs::current_path() / "custom/shape_predictor_68_face_landmarks.dat";
    if (!fs::exists(model))
        model = fs::current_path() / "build/combined/bin/Release/custom/shape_predictor_68_face_landmarks.dat";
    if (!fs::exists(model)) { std::cerr << "Missing dlib predictor: " << model << "\n"; return 1; }

    dlib::frontal_face_detector faceDetector = dlib::get_frontal_face_detector();
    dlib::shape_predictor predictor;
    try { dlib::deserialize(model.string()) >> predictor; }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }

    cv::VideoCapture capture;
    cv::Mat frame;
    const int cameraIndex = argc > 1 ? std::atoi(argv[1]) : 0;
    if (!openCamera(cameraIndex, capture, frame)) { std::cerr << "Could not open camera " << cameraIndex << "\n"; return 1; }
    const cv::Size cameraSize = frame.size();

    Shared shared;
    std::thread worker(trackingWorker, std::ref(capture), cameraSize, std::ref(faceDetector),
                       std::cref(predictor), std::ref(shared));

    InitWindow(kWinW, kWinH, "Tesseract head-tracked parallax");
    SetTargetFPS(60);

    // Initial camera distance (~600 mm from the screen) before any tracking data arrives.
    const float initUnitsPerMm = kScreenHeightUnits / (kMonitorHeightMm * kWinH / float(kMonitorHeightPx));
    ViewerPose viewer = {{0, kScreenCenterY, kScreenPlaneZ + 600 * initUnitsPerMm},
                         {0, kScreenCenterY, kScreenPlaneZ + 600 * initUnitsPerMm - 1}, {0, 1, 0}};
    Vector3 targetPos = viewer.position;
    bool physicalAlignment = false;  // P toggles: true = use webcam offset so the window matches real geometry
    bool fullscreen = false;         // F11 toggles borderless fullscreen
    WindowedState windowed;

    const FloatingShape shapes[] = {
        {ShapeType::Cube,{-4.2f,3.4f,-1.5f},1.0f,RED},
        {ShapeType::Sphere,{-1.8f,4.0f,2.0f},1.2f,BLUE},
        {ShapeType::Cylinder,{2.1f,3.5f,-4.0f},1.4f,GREEN},
        {ShapeType::Cone,{4.6f,2.3f,3.3f},1.2f,ORANGE},
        {ShapeType::Capsule,{-5.0f,0.1f,1.0f},1.3f,PURPLE},
        {ShapeType::Sphere,{-2.6f,0.4f,6.4f},1.1f,GOLD},
        {ShapeType::Cube,{0.2f,1.6f,-6.0f},1.3f,SKYBLUE},
        {ShapeType::Cylinder,{3.4f,0.2f,0.0f},1.5f,PINK},
        {ShapeType::Cone,{5.0f,-2.1f,1.8f},1.4f,LIME},
        {ShapeType::Capsule,{1.4f,-2.6f,6.6f},1.2f,VIOLET},
        {ShapeType::Cube,{-1.8f,-3.0f,-2.5f},1.1f,MAROON},
        {ShapeType::Sphere,{-4.5f,-2.0f,-5.0f},1.3f,DARKBLUE},
        {ShapeType::Cylinder,{2.7f,-3.4f,-5.5f},1.3f,DARKGREEN},
        {ShapeType::Cone,{0.2f,4.1f,6.8f},1.0f,BROWN},
        {ShapeType::Capsule,{4.0f,-0.9f,-3.0f},1.4f,DARKPURPLE}
    };

    const float backZ = -12.0f;

    while (!WindowShouldClose() && !shared.quit) {
        if (IsKeyPressed(KEY_R)) shared.recalibrate = true;
        if (IsKeyPressed(KEY_P)) physicalAlignment = !physicalAlignment;
        if (IsKeyPressed(KEY_F11)) { fullscreen = !fullscreen; SetBorderlessFullscreen(fullscreen, windowed); }

        // Recomputed every frame so windowed and fullscreen both map to true physical size.
        const int   sw = GetScreenWidth(), sh = GetScreenHeight();
        const float monPx = float(GetMonitorHeight(GetCurrentMonitor()));
        const float physHeightMm = kMonitorHeightMm * sh / monPx;   // physical height of the drawn area
        const float unitsPerMm = kScreenHeightUnits / physHeightMm;
        const float screenWidth = kScreenHeightUnits * sw / float(sh);
        const float L = -screenWidth / 2, Rr = screenWidth / 2;
        const float B = kScreenCenterY - screenHeight / 2, T = kScreenCenterY + screenHeight / 2;

        Snapshot snap;
        { std::lock_guard<std::mutex> lk(shared.m); snap = shared.snap; }

        // Camera X points to image-right, which is the user's LEFT, so X is negated.
        // Camera Y points down; raylib Y is up, so Y is negated too.
        // When tracking is lost, the last target is held.
        if (snap.valid && (physicalAlignment || snap.calibrated)) {
            double xMm, yMm;
            if (physicalAlignment) {
                xMm = kCameraOffsetXMm - snap.eye[0];
                yMm = kCameraOffsetYMm - snap.eye[1];
            } else {
                xMm = -(snap.eye[0] - snap.reference[0]);
                yMm = -(snap.eye[1] - snap.reference[1]);
            }
            targetPos = { float(xMm * unitsPerMm),
                          kScreenCenterY + float(yMm * unitsPerMm),
                          kScreenPlaneZ + float(snap.eye[2] * unitsPerMm) };
        }
        // Tracking runs ~30 Hz; ease toward it at render rate for smooth motion.
        const float k = 1.0f - std::exp(-GetFrameTime() * 25.0f);
        viewer.position = Vector3Add(viewer.position, Vector3Scale(Vector3Subtract(targetPos, viewer.position), k));
        viewer.target = {viewer.position.x, viewer.position.y, viewer.position.z - 1.0f};

        Camera3D camera = CreateCamera(viewer);
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        ApplyOffAxisProjection(viewer, screenWidth, screenHeight, viewer.position.z - kScreenPlaneZ, nearPlane, 150.0f);

        // Depth cues: the screen frame plus a wireframe "room" behind it.
        const Vector2 corners[4] = {{L, B}, {Rr, B}, {Rr, T}, {L, T}};
        for (int i = 0; i < 4; ++i) {
            const Vector2 a = corners[i], b = corners[(i + 1) % 4];
            DrawLine3D({a.x, a.y, kScreenPlaneZ}, {b.x, b.y, kScreenPlaneZ}, DARKGRAY);
            DrawLine3D({a.x, a.y, backZ}, {b.x, b.y, backZ}, LIGHTGRAY);
            DrawLine3D({a.x, a.y, kScreenPlaneZ}, {a.x, a.y, backZ}, LIGHTGRAY);
        }
        for (const auto& shape : shapes) DrawFloatingShape(shape);
        EndMode3D();

        DrawFPS(10, 10);
        DrawText("R recenter | P mode | F11 fullscreen | ESC close", 10, 42, 18, DARKBLUE);
        DrawText(TextFormat("Mode: %s | Eye (raylib): X %.2f Y %.2f Z %.2f",
                 physicalAlignment ? "physical" : "calibrated",
                 viewer.position.x, viewer.position.y, viewer.position.z), 10, 67, 18, DARKGRAY);
        DrawText(TextFormat("%s | calibration %d/%d | reprojection %.1f px",
                 snap.status.c_str(), snap.collected, kCalibrationFrames, snap.reprojErr), 10, 92, 18, DARKGRAY);
        EndDrawing();
    }

    shared.quit = true;
    worker.join();
    capture.release();
    CloseWindow();
    return 0;
}