#include "core/config.h"

#include <raylib.h>
#include <opencv2/core.hpp>
#include <opencv2/core/persistence.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace tess {

double DiagonalToHorizontalFov(double diagDeg) {
    const double diagPx = std::hypot(double(kCamW), double(kCamH));
    return 2.0 * std::atan(std::tan(diagDeg * kPi / 360.0) * kCamW / diagPx) * 180.0 / kPi;
}

double Config::hfovDeg() const {
    if (!(fovDeg > 10 && fovDeg < 170)) return kDefaultHFovDeg;
    return fovDiagonal ? DiagonalToHorizontalFov(fovDeg) : fovDeg;
}

bool Config::load(const std::string& path) {
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
    rd("cam_tilt_deg", camTiltDeg);
    ri("camera_index", cameraIndex); ri("webcam_view", webcamView);
    rd("strength", strength); ri("scene", scene);
    rb("prediction", prediction); rb("show_hud", showHud);
    strength = std::clamp(strength, 0.1, 1.5);
    camTiltDeg = std::clamp(camTiltDeg, -kMaxTiltDeg, kMaxTiltDeg);
    cameraIndex = std::max(0, cameraIndex);
    webcamView = std::clamp(webcamView, 0, 2);
    if (photoDone && !(screenHmm > 10)) photoDone = false;
    return true;
}

bool Config::save(const std::string& path) const {
    cv::FileStorage fs;
    try { if (!fs.open(path, cv::FileStorage::WRITE)) return false; } catch (const cv::Exception&) { return false; }
    fs << "screen_diag_in" << screenDiagIn << "screen_w_mm" << screenWmm << "screen_h_mm" << screenHmm;
    fs << "cam_offset_x_mm" << camOffXmm << "cam_offset_y_mm" << camOffYmm << "cam_offset_z_mm" << camOffZmm;
    fs << "photo_done" << int(photoDone);
    fs << "fov_deg" << fovDeg << "fov_diagonal" << int(fovDiagonal);
    fs << "cam_tilt_deg" << camTiltDeg;
    fs << "camera_index" << cameraIndex << "webcam_view" << webcamView;
    fs << "strength" << strength << "scene" << scene;
    fs << "prediction" << int(prediction) << "show_hud" << int(showHud);
    return true;
}

void Config::resetTuning() {
    strength = kDefaultStrength;
    fovDeg = 0; fovDiagonal = true;
    camTiltDeg = 0;
    prediction = true;
    showHud = true;
}

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

} // namespace tess
