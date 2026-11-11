#include "render/viewer.h"

#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

namespace tess {
namespace {

// Latency compensation.
constexpr double kExtraLatencyS     = 0.045;
constexpr double kMaxExtrapolationS = 0.10;
constexpr double kPredictMinSpeed   = 40.0;   // mm/s: no prediction below this (prediction amplifies noise)
constexpr double kPredictFullSpeed  = 150.0;  // mm/s
constexpr float  kRenderSmoothRate  = 60.0f;

} // namespace

ScreenGeometry ComputeScreenGeometry(const ScreenDims& dims, int mon) {
    ScreenGeometry g;
    g.sw = GetScreenWidth();
    g.sh = std::max(1, GetScreenHeight());
    g.monWpx = float(std::max(1, GetMonitorWidth(mon)));
    g.monHpx = float(std::max(1, GetMonitorHeight(mon)));
    g.monAspect = double(g.monWpx) / g.monHpx;
    g.mmPerPxX = float(dims.wMm / g.monWpx);
    g.mmPerPxY = float(dims.hMm / g.monHpx);
    g.physHeightMm = g.mmPerPxY * g.sh;
    g.physWidthMm = g.mmPerPxX * g.sw;
    g.unitsPerMm = kScreenHeightUnits / g.physHeightMm;
    g.screenWidth = g.physWidthMm * g.unitsPerMm;
    g.screenHeight = kScreenHeightUnits;
    g.L = -g.screenWidth / 2; g.R = g.screenWidth / 2;
    g.B = kScreenCenterY - g.screenHeight / 2; g.T = kScreenCenterY + g.screenHeight / 2;
    const Vector2 wp = GetWindowPosition(), mp = GetMonitorPosition(mon);
    g.winOffXMm =  ((wp.x - mp.x) + g.sw * 0.5f - g.monWpx * 0.5f) * g.mmPerPxX;
    g.winOffYMm = -((wp.y - mp.y) + g.sh * 0.5f - g.monHpx * 0.5f) * g.mmPerPxY;
    return g;
}

void ViewerRig::init(const ScreenDims& dims) {
    eyeWorld = {0, kScreenCenterY, kScreenPlaneZ + 600.0f * float(kScreenHeightUnits / dims.hMm)};
    targetPos = eyeWorld;
    predictS = 0;
}

void ViewerRig::update(const Snapshot& snap, const ScreenGeometry& g, bool absolute, double strength, bool prediction) {
    predictS = 0.0;
    if (snap.calibrated && snap.reference[2] > 150) neutralDistMm = float(snap.reference[2]);
    if (snap.valid && (absolute || snap.calibrated)) {
        if (prediction) {
            const double speed = cv::norm(snap.vel);
            const double gg = std::clamp((speed - kPredictMinSpeed) / (kPredictFullSpeed - kPredictMinSpeed), 0.0, 1.0);
            predictS = gg * gg * (3 - 2 * gg) *
                       std::clamp(Seconds(Clock::now() - snap.stamp) + kExtraLatencyS + 1.0 / kRenderSmoothRate, 0.0,
                                  kMaxExtrapolationS);
        }
        const cv::Vec3d eye = snap.pos + snap.vel * predictS;
        double xMm, yMm;
        if (absolute) { xMm = (eye[0] - g.winOffXMm) * strength; yMm = (eye[1] - g.winOffYMm) * strength; }
        else          { xMm = (eye[0] - snap.reference[0]) * strength; yMm = (eye[1] - snap.reference[1]) * strength; }
        const double zMm = std::max(eye[2], 120.0);
        targetPos = {float(xMm * g.unitsPerMm), kScreenCenterY + float(yMm * g.unitsPerMm),
                     kScreenPlaneZ + float(zMm * g.unitsPerMm)};
    }
    const float k = 1.0f - std::exp(-GetFrameTime() * kRenderSmoothRate);
    eyeWorld = Vector3Add(eyeWorld, Vector3Scale(Vector3Subtract(targetPos, eyeWorld), k));
}

float ViewerRig::screenDist() const { return std::max(0.5f, eyeWorld.z - kScreenPlaneZ); }

Vector3 ViewerRig::eyeMm(const ScreenGeometry& g) const {
    return Vector3Scale(Vector3Subtract(eyeWorld, {0, kScreenCenterY, kScreenPlaneZ}), 1.0f / g.unitsPerMm);
}

void ApplyOffAxisProjection(Vector3 eye, float screenW, float screenH, float screenDist) {
    const float n = kNearPlane;
    const float left   = n * (-screenW * 0.5f - eye.x) / screenDist;
    const float right  = n * ( screenW * 0.5f - eye.x) / screenDist;
    const float bottom = n * (-screenH * 0.5f - (eye.y - kScreenCenterY)) / screenDist;
    const float top    = n * ( screenH * 0.5f - (eye.y - kScreenCenterY)) / screenDist;
    rlSetMatrixProjection(MatrixFrustum(left, right, bottom, top, n, kFarPlane));
}

void SetBorderlessFullscreen(bool on, WindowedState& saved) {
    if (on) {
        const Vector2 p = GetWindowPosition();
        saved = { (int)p.x, (int)p.y, GetScreenWidth(), GetScreenHeight() };
        const int mon = GetCurrentMonitor();
        const Vector2 mp = GetMonitorPosition(mon);
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowPosition((int)mp.x, (int)mp.y);
        SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowSize(saved.w, saved.h);
        SetWindowPosition(saved.x, saved.y);
    }
}

} // namespace tess
