// The interface every on-screen overlay implements: HUD, start menu, settings panel, calibration pages...
//
// How a layer fits into the frame (see main.cpp):
//   1. update() is called on every layer, topmost first, until one returns true (it owns the input this frame:
//      a modal panel is open, or the layer is a full page). If none does and the Scene page is showing,
//      the scene hotkeys run.
//   2. The 3D scene is drawn (on the Scene page, and behind the scene selector when previewing).
//   3. draw() is called on every layer, bottom-most first, so later layers appear on top. Widgets are
//      immediate-mode (ui_widgets.h): drawing a button also reports its click, so draw() may change AppState.
//
// A layer changes the program only through AppState (core/app_state.h). It never talks to the tracker or the
// renderer directly, so layers can be added and removed without touching anything else.
#pragma once
#include "core/app_state.h"
#include "render/viewer.h"
#include "tracking/head_tracker.h"

#include <raylib.h>
#include <cstdint>

namespace tess {

// Read-only per-frame information for UI code.
struct UiFrame {
    const Snapshot& snap;         // latest tracker result (status text, eye position in mm, camera fps...)
    const ScreenDims& dims;       // physical screen size and where it came from
    const ScreenGeometry& geo;    // window size in pixels (geo.sw, geo.sh), monitor size, mm per pixel...
    Vector3 eyeMm{};              // smoothed eye relative to the window center, mm (filled in before draw())
    double predictS = 0;          // latency prediction applied this frame, seconds (filled in before draw())
    double mouseIdleS = 0;        // seconds since the mouse last moved
    const cv::Mat* webcamRgba = nullptr;   // webcam diagnostics image (picture-in-picture mode only)
    std::uint64_t webcamSeq = 0;           // changes when webcamRgba holds a new frame
};

class UiLayer {
public:
    virtual ~UiLayer() = default;

    // Keyboard handling and the "do I own the input this frame" decision. Return true when this layer is
    // showing something modal (its page, an open panel): layers below and the scene hotkeys are then skipped.
    virtual bool update(AppState&, const UiFrame&) { return false; }

    // Draw in 2D on top of the scene (between EndMode3D and EndDrawing). May change AppState through widgets.
    virtual void draw(AppState& state, const UiFrame& frame) = 0;
};

} // namespace tess
