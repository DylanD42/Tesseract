// Constants shared by the whole program and the persistent configuration (tesseract_calibration.yml).
#pragma once
#include <string>

namespace tess {

inline constexpr double kPi = 3.14159265358979323846;

// ---- window and world units ----
inline constexpr int   kWinW = 1000, kWinH = 800;
inline constexpr float kScreenHeightUnits = 10.0f;   // world units spanned by the window height
inline constexpr float kScreenPlaneZ = 1.0f;         // the "glass" sits at this world Z
inline constexpr float kScreenCenterY = 1.0f;
inline constexpr float kNearPlane = 0.3f, kFarPlane = 5000.0f;

inline constexpr const char* kConfigFile = "tesseract_calibration.yml";

// ---- webcam: 640x480 (fast and steady; higher resolutions cost more than they gained) ----
inline constexpr int kCamW = 640, kCamH = 480;

// ---- defaults used until setup has been done ----
inline constexpr double kDefaultScreenHeightMm = 300.0;  // only if the monitor does not report its size
inline constexpr double kWebcamAboveTopEdgeMm  = 10.0;   // assumed webcam position: centered, just above the top edge
inline constexpr double kDefaultHFovDeg        = 70.0;   // webcam horizontal field of view until one is entered
inline constexpr double kDefaultStrength       = 0.75;   // fraction of physical motion (1.0 = exact)
inline constexpr double kMaxTiltDeg            = 45.0;

// Horizontal FOV of the 640x480 image from a diagonal FOV (spec sheets usually give the diagonal).
double DiagonalToHorizontalFov(double diagDeg);

// Everything that survives a restart. Edit through AppState so the save and the tracker update happen.
struct Config {
    // Screen photo calibration.
    double screenDiagIn = 0, screenWmm = 0, screenHmm = 0;
    double camOffXmm = 0, camOffYmm = 0, camOffZmm = 0;             // webcam lens vs screen center (+Y up, +Z toward you)
    bool   photoDone = false;
    // Webcam.
    double fovDeg = 0;            // as entered; 0 = not entered (default horizontal FOV is used)
    bool   fovDiagonal = true;    // how fovDeg was entered
    double camTiltDeg = 0;        // webcam pitched down toward you (+) or up (-)
    int    cameraIndex = 0;
    int    webcamView = 1;        // 0 off, 1 separate window, 2 picture-in-picture (see DebugView)
    // Parallax.
    double strength = kDefaultStrength;
    int    scene = 0;
    bool   prediction = true;     // latency compensation
    bool   showHud = true;
    // Imported models.
    bool   modelAutoplay = true;  // start a model's animation when its scene opens
    bool   modelBackdrop = true;  // studio box room around imported models

    double hfovDeg() const;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    void resetTuning();           // strength, FOV, tilt, prediction, HUD back to defaults (keeps photo, camera, scene)
};

// Physical screen size: photo setup if done, else what the monitor reports (EDID), else a guess.
struct ScreenDims { double wMm = 0, hMm = 0; std::string source; };

ScreenDims ResolveScreen(const Config& cfg, int monitor);

} // namespace tess
