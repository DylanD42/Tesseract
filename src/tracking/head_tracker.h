// Head tracking: webcam -> YuNet face box -> dlib 68 landmarks -> solvePnP head pose -> filtered eye position.
//
// Runs on its own two threads (grab + tracking). The rest of the program only ever calls the HeadTracker
// methods below and reads Snapshot copies; it never touches OpenCV capture or dlib directly.
//
// Coordinates: Snapshot positions are SCREEN space millimetres: origin at the monitor center,
// +X to your right, +Y up, +Z toward you.
#pragma once
#include "core/config.h"

#include <opencv2/core.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace tess {

using Clock = std::chrono::steady_clock;
inline double Seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

// What the tracker publishes for the render thread, once per processed camera frame.
struct Snapshot {
    bool valid = false;        // have a filtered eye position
    bool calibrated = false;   // recenter reference captured
    bool tracking = false;     // this frame produced a measurement
    int collected = 0, faceCount = 0;
    double reprojErr = 0;
    double faceScore = 0;
    double procMs = 0, workMs = 0, camFps = 0;
    double rawZ = 0;               // unfiltered eye distance (shown while entering the FOV)
    cv::Vec3d pos{0, 0, 0};        // filtered eye, screen space mm
    cv::Vec3d vel{0, 0, 0};        // mm/s
    cv::Vec3d reference{0, 0, 0};  // recentered-mode reference (screen space)
    Clock::time_point stamp{};
    int camW = 0, camH = 0;
    std::string status = "Camera off";
};

// Camera model inputs that the settings panel can change at runtime.
struct TrackerParams {
    double hfovDeg = kDefaultHFovDeg;
    cv::Vec3d offset{0, 0, 0};     // webcam lens position in screen space (mm)
    double tiltDeg = 0;            // webcam pitched down toward you (+) or up (-), degrees
};

TrackerParams MakeTrackerParams(const Config& cfg, const ScreenDims& dims);

// Where the webcam diagnostics image goes.
enum class DebugView { Off = 0, Window = 1, PictureInPicture = 2 };

class HeadTracker {
public:
    HeadTracker();
    ~HeadTracker();
    HeadTracker(const HeadTracker&) = delete;
    HeadTracker& operator=(const HeadTracker&) = delete;

    // Loads dlib and YuNet once. On failure returns false and fills `error`.
    bool loadModels(const std::filesystem::path& landmarkModel, const std::filesystem::path& yunetModel, std::string& error);

    // Opens the webcam and starts the threads. Blocks for up to a few seconds while the camera opens, so call it
    // from a worker (std::async) if the window must stay responsive. Not thread-safe against the other methods
    // except snapshot()/setParams()/recenter()/setEyeSelect()/setDebugView(), which are fine to call meanwhile.
    bool startCamera(int cameraIndex, std::string& error);
    void stopCamera();                           // joins the threads and releases the camera
    bool cameraActive() const;

    Snapshot snapshot() const;                   // latest result (a copy; safe to call every frame)
    void setParams(const TrackerParams& p);      // webcam FOV, lens position or tilt changed
    void recenter();                             // capture a new sitting-position reference (R)
    void setEyeSelect(int sel);                  // 0 midpoint, 1 left eye, 2 right eye (pen-tip test)

    void setDebugView(DebugView view);
    // Copies the newest diagnostics image (RGBA, mirrored) if its sequence number is newer than `seq`.
    // Only produced in PictureInPicture mode.
    bool debugImage(cv::Mat& rgba, std::uint64_t& seq) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tess
