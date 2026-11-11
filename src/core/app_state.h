// The single source of truth for everything the user can change at runtime.
//
// UI code (menus, settings panel, HUD, calibration screens) reads and writes this struct and nothing else.
// Changes that need follow-up work are recorded as requests; main.cpp applies them once per frame:
//   configDirty    -> cfg is written to tesseract_calibration.yml
//   paramsDirty    -> the tracker receives the new webcam FOV / lens position / tilt
//   recenter       -> the tracker captures a new sitting-position reference
//   cameraRequest  -> the tracker is (re)started on that camera index, or stopped (-1)
//   refreshCameras -> the camera list is enumerated again
//   fullscreen     -> the window is toggled when it differs from what is applied
//   importRequests -> model files are copied into the library and the new scene is opened
//   removeScene    -> that imported scene is deleted from the library
#pragma once
#include "core/config.h"
#include "render/model_library.h"
#include "tracking/camera_list.h"

#include <optional>
#include <string>
#include <vector>

namespace tess {

// Full-window pages. Settings is a modal panel that can sit on top of StartMenu or Scene.
enum class Page { StartMenu, SceneSelect, Scene, Calibration, PenTipTest };

enum class WebcamView { Off = 0, Window = 1, PictureInPicture = 2 };

// Short message at the bottom of the window.
struct Toast {
    std::string text;
    double until = 0;             // GetTime() at which it disappears
    void show(const std::string& msg, double seconds = 7);
    bool visible() const;
};

struct AppState {
    Config      cfg;              // persisted settings
    std::string configPath;

    // Runtime (not saved).
    bool physicalAlignment = false;   // true geometry (needs the webcam position) vs recentered on the user
    bool fullscreen = false;
    int  eyeSelect = 0;               // 0 = midpoint, 1 = left eye, 2 = right eye (pen-tip test)
    double penTipMm = 100;            // pen-tip test distance

    // Navigation.
    Page page = Page::StartMenu;
    bool settingsOpen = false;
    Page pageUnderSettings = Page::StartMenu;   // where Back from settings returns to

    // Camera status (written by main.cpp, read by the settings panel).
    std::vector<CameraInfo> cameras;
    bool cameraActive = false;
    bool cameraStarting = false;
    std::string cameraStatus;

    Toast toast;

    // Animation of the imported model being shown (main.cpp advances it; the model controls change it).
    ModelPlayback playback;

    // Requests for main.cpp (cleared once applied).
    bool configDirty = false;
    bool paramsDirty = false;
    bool recenter = false;
    bool refreshCameras = false;
    bool quit = false;
    std::optional<int> cameraRequest;   // camera index to start/switch to, -1 = stop
    std::vector<std::string> importRequests;   // model files (UTF-8) from the Import button or drag-and-drop
    std::optional<int> removeScene;            // scene index of an imported scene to delete

    // ---- actions: use these rather than poking cfg so the save / tracker update happens ----
    void enterScene(int index);               // from the scene picker: selects, saves, shows the scene
    void importModel(const std::string& pathUtf8) { importRequests.push_back(pathUtf8); }
    void setStrength(double value);           // clamps to [0.1, 1.5]; saves
    void setFov(double deg, bool diagonal);   // clamps to [20, 160]; updates the tracker; saves
    void setTilt(double deg);                 // clamps to +-kMaxTiltDeg; updates the tracker; saves
    void setWebcamView(WebcamView view);      // saves (main.cpp pushes it to the tracker)
    void togglePhysicalAlignment();
    void requestSave() { configDirty = true; }

    void openSettings();                      // over the current page
    void closeSettings();
    void openPage(Page p);                    // Calibration / PenTipTest from the settings panel
    void backToSettings();                    // from Calibration / PenTipTest

    WebcamView webcamView() const { return WebcamView(cfg.webcamView); }
    std::string fovLabel() const;             // e.g. "78.0 deg diagonal = 70.4 deg horizontal"
};

} // namespace tess
