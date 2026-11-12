// Turns the tracked eye position into a virtual camera: physical screen -> world units, smoothing,
// latency prediction, and the off-axis (asymmetric frustum) projection through the screen rectangle.
#pragma once
#include "core/config.h"
#include "tracking/head_tracker.h"

#include <raylib.h>

namespace tess {

// Per-frame geometry of the window on the physical monitor.
struct ScreenGeometry {
    int   sw = 1, sh = 1;              // window size in pixels
    float monWpx = 1, monHpx = 1;      // monitor size in pixels
    double monAspect = 1;
    float mmPerPxX = 0, mmPerPxY = 0;
    float physWidthMm = 0, physHeightMm = 0;   // the drawn area, in mm
    float unitsPerMm = 0;              // world units per millimetre
    float screenWidth = 0, screenHeight = 0;   // the drawn area, in world units
    float L = 0, R = 0, B = 0, T = 0;  // the opening (glass rectangle) in world units
    float winOffXMm = 0, winOffYMm = 0;        // window center relative to the monitor center, mm (+Y up)
};

ScreenGeometry ComputeScreenGeometry(const ScreenDims& dims, int monitor);

// The virtual eye. Smoothed toward the tracked position every frame.
struct ViewerRig {
    Vector3 eyeWorld{};
    Vector3 targetPos{};
    double  predictS = 0;      // seconds of latency prediction applied this frame (for the HUD)
    float   neutralDistMm = 600;   // sitting distance from the screen (recenter reference); imported cameras sit here

    void init(const ScreenDims& dims);
    // absolute: eye relative to the window center (true geometry); otherwise relative to the
    // recentered sitting position. strength scales sideways/vertical motion (1.0 = physically exact).
    void update(const Snapshot& snap, const ScreenGeometry& g, bool absolute, double strength, bool prediction);
    // Scene-selector preview: the mouse stands in for the head. Mouse at the window center = straight on;
    // at the edges the eye is about 20 cm to the side / 12 cm up or down, at the sitting distance.
    void updateFromMouse(const ScreenGeometry& g, Vector2 mouse);

    float   screenDist() const;                       // eye to glass, world units
    Vector3 eyeMm(const ScreenGeometry& g) const;     // eye relative to the window center, mm
};

// Asymmetric frustum from the eye through the physical screen rectangle. Call inside BeginMode3D.
void ApplyOffAxisProjection(Vector3 eye, float screenW, float screenH, float screenDist);

struct WindowedState { int x = 100, y = 100, w = kWinW, h = kWinH; };
void SetBorderlessFullscreen(bool on, WindowedState& saved);

} // namespace tess
