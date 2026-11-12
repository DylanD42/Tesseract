// Tesseract: head-tracked "window" parallax.
// YuNet face box -> dlib 68 landmarks -> solvePnP head pose -> raylib off-axis projection.
//
// Threads
//   grab + tracking (tracking/head_tracker.cpp) : webcam -> filtered eye position, published as Snapshot
//   render (this file)                          : off-axis frustum from the eye through the physical screen, scenes, UI
//
// This file only wires the pieces together. Behaviour lives in:
//   core/app_state.h   everything the UI can change, and how changes reach the rest of the program
//   render/scenes.cpp  the scene list (built-in scenes + imported models from render/model_library.cpp)
//   ui/                start menu, scene picker, settings panel, calibration pages, HUD. New UI goes in `layers`.
#include "core/app_state.h"
#include "core/assets.h"
#include "core/config.h"
#include "render/model_library.h"
#include "render/scene_shader.h"
#include "render/scene_textures.h"
#include "render/scenes.h"
#include "render/viewer.h"
#include "tracking/camera_list.h"
#include "tracking/head_tracker.h"
#include "ui/calibration_ui.h"
#include "ui/hotkeys.h"
#include "ui/hud.h"
#include "ui/menu_ui.h"
#include "ui/model_controls_ui.h"
#include "ui/settings_ui.h"
#include "ui/ui_layer.h"
#include "ui/webcam_pip.h"

#include <raylib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace tess;

int main(int argc, char** argv) {
    // ---- settings ----
    AppState st;
    st.configPath = std::filesystem::absolute(kConfigFile).string();
    st.cfg.load(st.configPath);
    InitModelLibrary(std::filesystem::path(st.configPath).parent_path() / "models");   // imported models
    st.cfg.scene = std::clamp(st.cfg.scene, 0, SceneCount() - 1);
    if (argc > 1) st.cfg.cameraIndex = std::atoi(argv[1]);   // optional: camera index on the command line
    // True geometry needs to know where the webcam is; until the photo setup is done, recenter on the user instead
    // (otherwise the view looks like it is peering down). Settings > True geometry toggles either way.
    st.physicalAlignment = st.cfg.photoDone;

    // ---- head tracking: models now, camera in the background so the menu appears at once ----
    HeadTracker tracker;
    {
        std::string err;
        if (!tracker.loadModels(FindAsset("shape_predictor_68_face_landmarks.dat"),
                                FindAsset("face_detection_yunet_2023mar.onnx"), err)) {
            std::cerr << err << "\n";
            return 1;
        }
    }
    st.cameras = ListCameras();
    std::future<std::pair<bool, std::string>> cameraStart;
    auto cameraName = [&](int index) {
        for (const CameraInfo& c : st.cameras) if (c.index == index) return c.name;
        return "camera " + std::to_string(index);
    };
    auto beginCameraStart = [&](int index) {
        st.cameraStarting = true;
        st.cameraActive = false;
        st.cameraStatus = "Opening " + cameraName(index) + "...";
        cameraStart = std::async(std::launch::async, [&tracker, index] {
            std::string err;
            const bool ok = tracker.startCamera(index, err);
            return std::make_pair(ok, err);
        });
    };
    beginCameraStart(st.cfg.cameraIndex);

    // ---- window ----
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(kWinW, kWinH, "Tesseract");
    SetWindowMinSize(960, 720);
    SetExitKey(KEY_NULL);
    SetTargetFPS(std::max(60, GetMonitorRefreshRate(GetCurrentMonitor())));

    SceneShader shader;
    shader.load();
    SceneTextures textures;
    textures.load();

    ScreenDims dims = ResolveScreen(st.cfg, GetCurrentMonitor());
    tracker.setParams(MakeTrackerParams(st.cfg, dims));
    tracker.setDebugView(DebugView(st.cfg.webcamView));
    int webcamViewApplied = st.cfg.webcamView;

    ViewerRig rig;
    rig.init(dims);
    WindowedState windowed;
    bool fullscreenApplied = false;
    cv::Mat pipImage;
    std::uint64_t pipSeq = 0;
    Vector2 lastMouse = GetMousePosition();
    double lastMouseMove = GetTime();

    // ---- UI layers, bottom to top. Add new layers here. ----
    Hud hud;
    WebcamPip pip;
    SceneOverlayUi sceneOverlay;
    ModelControlsUi modelControls;
    MenuUi menu;
    CalibrationUi calibration;
    SettingsUi settings;
    std::vector<UiLayer*> layers = {&hud, &pip, &sceneOverlay, &modelControls, &menu, &calibration, &settings};

    while (!st.quit && !WindowShouldClose()) {
        // ---- camera lifecycle ----
        if (cameraStart.valid() && cameraStart.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto [ok, err] = cameraStart.get();
            st.cameraStarting = false;
            st.cameraActive = ok;
            st.cameraStatus = ok ? cameraName(st.cfg.cameraIndex) + " connected" : err;
            if (!ok) st.toast.show(err + ". Choose another camera in Settings.", 8);
        }
        if (st.cameraRequest && !st.cameraStarting) {
            const int index = *st.cameraRequest;
            st.cameraRequest.reset();
            if (st.cameraActive) tracker.stopCamera();
            st.cameraActive = false;
            if (index < 0) {
                st.cameraStatus = "Camera stopped";
            } else {
                st.cfg.cameraIndex = index;
                st.requestSave();
                beginCameraStart(index);
            }
        }
        if (st.refreshCameras) { st.cameras = ListCameras(); st.refreshCameras = false; }
        if (st.cfg.webcamView != webcamViewApplied) {
            tracker.setDebugView(DebugView(st.cfg.webcamView));
            webcamViewApplied = st.cfg.webcamView;
        }

        const Snapshot snap = tracker.snapshot();

        // ---- geometry: physical screen -> world units (the window may have moved or changed monitor) ----
        const int mon = GetCurrentMonitor();
        const ScreenDims newDims = ResolveScreen(st.cfg, mon);
        if (std::abs(newDims.hMm - dims.hMm) > 0.5 || std::abs(newDims.wMm - dims.wMm) > 0.5) {
            dims = newDims;
            tracker.setParams(MakeTrackerParams(st.cfg, dims));
        }
        const ScreenGeometry geo = ComputeScreenGeometry(dims, mon);
        UiFrame frame{snap, dims, geo};
        {
            const Vector2 m = GetMousePosition();
            if (m.x != lastMouse.x || m.y != lastMouse.y) { lastMouse = m; lastMouseMove = GetTime(); }
            frame.mouseIdleS = GetTime() - lastMouseMove;
        }
        if (st.cfg.webcamView == int(WebcamView::PictureInPicture) && st.page == Page::Scene) {
            tracker.debugImage(pipImage, pipSeq);
            frame.webcamRgba = &pipImage;
            frame.webcamSeq = pipSeq;
        }

        // ---- input: topmost layer first; the scene hotkeys only when nobody owns the input ----
        HandleGlobalHotkeys(st);
        bool consumed = false;
        for (auto it = layers.rbegin(); it != layers.rend() && !consumed; ++it) consumed = (*it)->update(st, frame);
        if (!consumed && st.page == Page::Scene) HandleSceneHotkeys(st);

        // ---- apply what the UI asked for ----
        if (st.recenter) { tracker.recenter(); st.recenter = false; }
        if (st.paramsDirty) {
            dims = ResolveScreen(st.cfg, mon);
            tracker.setParams(MakeTrackerParams(st.cfg, dims));
            st.paramsDirty = false;
        }
        if (st.configDirty && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {   // not while a slider is being dragged
            if (!st.cfg.save(st.configPath)) st.toast.show("Could not write " + st.configPath);
            st.configDirty = false;
        }
        // ---- imported models: drag-and-drop (the calibration page takes dropped photos itself), import, delete ----
        if (st.page != Page::Calibration && IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            int unsupported = 0;
            for (unsigned i = 0; i < dropped.count; ++i) {
                const std::string path = dropped.paths[i];
                const bool blend = path.size() > 6 && path.compare(path.size() - 6, 6, ".blend") == 0;
                if (IsSupportedModelFile(path) || blend) st.importModel(path);
                else ++unsupported;
            }
            UnloadDroppedFiles(dropped);
            if (unsupported && st.importRequests.empty())
                st.toast.show(std::string("Not a 3D model. Drop one of: ") + kSupportedModelList, 6);
        }
        for (const std::string& path : st.importRequests) {
            std::string err, warning;
            const int index = ImportModel(path, err, warning);
            if (index < 0) { st.toast.show("Import failed: " + err, 9); continue; }
            // Show it in the scene selector, previewed, with the (optional) description box open.
            const int scene = BuiltInSceneCount() + index;
            st.settingsOpen = false;
            st.page = Page::SceneSelect;
            st.previewSceneAt(scene);
            st.describeScene = scene;
            const LoadedModel* m = GetLoadedModel(index);
            std::string msg = "Imported " + SceneName(scene);
            msg += m && m->hasCamera ? " (using its camera)" : " (no camera in the file: fitted to the window)";
            if (m && m->clipCount() > 0) msg += ", " + std::to_string(m->clipCount()) + " animation(s)";
            if (!warning.empty()) msg += ". " + warning;
            st.toast.show(msg, 9);
        }
        st.importRequests.clear();
        if (st.removeScene) {
            const int scene = *st.removeScene;
            st.removeScene.reset();
            const std::string name = SceneName(scene);
            std::string err;
            if (IsImportedScene(scene) && RemoveImportedModel(ImportedIndex(scene), err)) {
                if (st.cfg.scene == scene) st.cfg.scene = 0;
                else if (st.cfg.scene > scene) --st.cfg.scene;
                if (st.previewScene == scene) st.previewScene = -1;
                else if (st.previewScene > scene) --st.previewScene;
                st.requestSave();
                st.toast.show("Deleted " + name, 4);
            } else if (!err.empty()) {
                st.toast.show("Delete failed: " + err, 6);
            }
        }

        if (st.fullscreen != fullscreenApplied) {
            SetBorderlessFullscreen(st.fullscreen, windowed);
            fullscreenApplied = st.fullscreen;
        }
        const bool hideCursor = st.page == Page::Scene && !st.settingsOpen && fullscreenApplied && frame.mouseIdleS > 2.5;
        if (hideCursor) HideCursor(); else ShowCursor();

        // ---- eye position -> virtual camera ----
        // True geometry: eye relative to the center of the drawn area. Recentered: relative to where you sat
        // during the first second (R). Strength scales the sideways/vertical motion.
        // The scene selector's preview uses the mouse instead (head tracking starts when the scene is entered).
        const int preview = st.page == Page::SceneSelect && !st.settingsOpen ? st.previewScene : -1;
        if (preview >= 0) rig.updateFromMouse(geo, GetMousePosition());
        else rig.update(snap, geo, st.physicalAlignment, st.cfg.strength, st.cfg.prediction);
        frame.eyeMm = rig.eyeMm(geo);
        frame.predictS = rig.predictS;
        const int shownScene = st.page == Page::Scene ? st.cfg.scene : preview;
        if (IsImportedScene(shownScene))
            if (const LoadedModel* m = GetLoadedModel(ImportedIndex(shownScene)))
                AdvancePlayback(st.playback, *m, GetFrameTime());

        // ---- draw: 3D scene (on the scene pages), then the UI layers on top ----
        BeginDrawing();
        const RenderInputs in{rig, geo, shader, textures, &st.playback};
        if (shownScene >= 0) RenderScene(shownScene, in);
        else ClearBackground(theme::kPanel);   // menu and calibration pages paint the whole window themselves
        for (UiLayer* layer : layers) layer->draw(st, frame);
        DrawToast(st.toast, geo.sw, geo.sh);
        EndDrawing();
    }

    if (cameraStart.valid()) cameraStart.wait();
    tracker.stopCamera();
    if (st.configDirty) st.cfg.save(st.configPath);
    ShutdownModelLibrary();
    calibration.unload();
    pip.unload();
    textures.unload();
    shader.unload();
    CloseWindow();
    return 0;
}
