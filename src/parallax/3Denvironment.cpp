#include "3Denvironment.h"
#include "settings.h"
#include "ui.h"
#include "off_axis_projection.h"
#include "window_motion.h"
#include "window_controls.h"
#include "start_menu.h"
#include "../scenes/project_scenes.h"
#include <raylib.h>
#include <rlgl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace tesseract {
using namespace settings;
namespace {
struct WindowedState {int x=100,y=100,w=kWinW,h=kWinH;};
void setBorderlessFullscreen(bool enabled,WindowedState& saved) {
    if (enabled) {
        const auto p=GetWindowPosition();
        saved={static_cast<int>(p.x),static_cast<int>(p.y),GetScreenWidth(),GetScreenHeight()};
        const int monitor=GetCurrentMonitor();
        const auto position=GetMonitorPosition(monitor);
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowPosition(static_cast<int>(position.x),static_cast<int>(position.y));
        SetWindowSize(GetMonitorWidth(monitor),GetMonitorHeight(monitor));
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowSize(saved.w,saved.h);
        SetWindowPosition(saved.x,saved.y);
    }
}
}
struct Environment3D::Impl {
    enum class Page { Start, SceneSelection, Playing };

    WindowMotion motion;
    WindowFeel feel;
    std::filesystem::path settingsPath;
    std::string settingsMessage;
    Page page=Page::Start;
    ProjectScene selectedScene=ProjectScene::FloatingShapes;
    bool controlsOpen=false,mouseInput=true,mouseOnly=false;
    bool fullscreen=false,hud=false,exitRequested=false;
    bool cameraActive=false,cameraStarting=false;
    std::optional<int> cameraRequest;
    double hintTime=8;
    double sceneElapsed=0;
    WindowedState windowed;

    Impl(const std::filesystem::path& applicationDirectory,bool previewOnly) {
        mouseOnly=previewOnly;
        mouseInput=true;
        settingsPath=applicationDirectory/"window_view.txt";
        try {if (std::filesystem::exists(settingsPath)) feel=loadWindowFeel(settingsPath);}
        catch (const std::exception& error) {settingsMessage=error.what();std::cerr << error.what() << '\n';}
        SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);
        InitWindow(kWinW,kWinH,"Tesseract");
        if (!IsWindowReady()) throw std::runtime_error("Could not create 3D window");
        SetWindowMinSize(960,720);
        SetExitKey(KEY_NULL);
        SetTargetFPS(std::max(60,GetMonitorRefreshRate(GetCurrentMonitor())));
        setBorderlessFullscreen(true,windowed);
        fullscreen=true;
    }
    ~Impl() {CloseWindow();}
    void drawFrame(const Snapshot& snapshot) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (controlsOpen) controlsOpen=false;
            else if (page==Page::SceneSelection||page==Page::Playing) page=Page::Start;
            else exitRequested=true;
        }

        if (page==Page::Playing&&!controlsOpen&&(IsKeyPressed(KEY_C)||IsKeyPressed(KEY_V)))
            controlsOpen=true;

        const bool modal=controlsOpen;
        if (page==Page::Playing&&!modal) {
            const auto actions=ui::pollActions();
            if (actions.toggleFullscreen) {fullscreen=!fullscreen;setBorderlessFullscreen(fullscreen,windowed);}
            if (actions.recenter) motion.recenter();
            if (IsKeyPressed(KEY_H)) hud=!hud;
            if (IsKeyPressed(KEY_M)&&cameraActive&&!mouseOnly) {mouseInput=!mouseInput;hintTime=8;}
        }
        if (page!=Page::Playing||modal||mouseInput||!fullscreen) ShowCursor(); else HideCursor();
        const int sw=std::max(1,GetScreenWidth()),sh=std::max(1,GetScreenHeight());
        const float dt=std::clamp(GetFrameTime(),0.0f,0.2f);
        BeginDrawing();
        if (page==Page::Playing) {
            sceneElapsed+=dt;
            if (mouseInput&&!modal) motion.setMouse({GetMouseX()/static_cast<double>(sw),GetMouseY()/static_cast<double>(sh)});
            else if (!mouseInput&&snapshot.faceCenterValid) motion.observe(snapshot.faceCenter,snapshot.faceSampleId);
            const auto offset=motion.update(dt,kWindowOpeningWidth,feel);
            const Vector3 eye{static_cast<float>(offset[0]),kScreenCenterY+static_cast<float>(offset[1]),
                              kScreenPlaneZ+static_cast<float>(offset[2])};
            const auto bounds=depthWindowBounds(sw,sh,kWindowOpeningWidth,kScreenCenterY,kScreenPlaneZ);
            const double farPlane=std::max(180.0,static_cast<double>(eye.z)+kWindowOpeningWidth*2+20);
            const auto matrices=makeOffAxisCamera(bounds,eye,0.05,farPlane);
            Camera3D camera{};
            camera.position=eye;camera.target={eye.x,eye.y,eye.z-1};camera.up={0,1,0};
            camera.fovy=45;camera.projection=CAMERA_PERSPECTIVE;
            ClearBackground(RAYWHITE);
            BeginMode3D(camera);
            rlSetMatrixModelview(matrices.view);
            rlSetMatrixProjection(matrices.projection);
            if (selectedScene==ProjectScene::FloatingShapes)
                drawFloatingShapeScene(kWindowOpeningWidth,static_cast<float>(bounds.top-bounds.bottom),
                                       kScreenPlaneZ,static_cast<float>(kWindowOpeningWidth/12.5f));
            else drawAnimatedSquareScene(kWindowOpeningWidth,kScreenCenterY,kScreenPlaneZ,sceneElapsed);
            EndMode3D();
            if (hud&&!controlsOpen) {
                DrawRectangle(0,0,sw,140,Fade(Color{235,241,245,255},0.89f));
                ui::drawOverlay(snapshot,{eye.x,eye.y,eye.z});
                DrawText(TextFormat("Sensitivity %.1f | smoothing %.2f | input: %s",
                    feel.sensitivity,feel.smoothing,mouseInput?"mouse":"camera"),10,116,15,DARKGRAY);
            }
            hintTime=std::max(0.0,hintTime-dt);
            if (!modal&&!hud&&hintTime>0) {
                DrawRectangleRounded({18,18,440,67},0.12f,8,Fade(Color{13,20,31,255},0.88f));
                DrawText(selectedScene==ProjectScene::FloatingShapes?"FLOATING SHAPES":"ANIMATED SQUARE",
                         34,31,20,Color{219,231,239,255});
                DrawText(mouseInput?"Mouse preview - move the pointer gently":"Head tracking - lean gently left and right",34,59,14,Color{144,172,190,255});
            }
            if (!modal) DrawText("C/V settings | M mouse | R center | H HUD | F11 fullscreen | ESC menu",18,sh-25,13,Color{130,160,182,255});
        } else {
            const auto action=ui::drawStartMenu(page==Page::SceneSelection,!controlsOpen);
            if (!controlsOpen) {
                switch (action) {
                case ui::MenuAction::SelectScene:
                    page=Page::SceneSelection;
                    break;
                case ui::MenuAction::SelectFloatingShapes:
                    selectedScene=ProjectScene::FloatingShapes;
                    sceneElapsed=0;
                    page=Page::Playing;
                    hintTime=8;
                    break;
                case ui::MenuAction::SelectAnimatedSquare:
                    selectedScene=ProjectScene::AnimatedSquare;
                    sceneElapsed=0;
                    page=Page::Playing;
                    hintTime=8;
                    break;
                case ui::MenuAction::OpenSettings:
                    controlsOpen=true;
                    break;
                case ui::MenuAction::Back:
                    page=page==Page::SceneSelection?Page::Start:Page::SceneSelection;
                    break;
                case ui::MenuAction::Exit:
                    exitRequested=true;
                    break;
                case ui::MenuAction::None:
                    break;
                }
            }
        }
        drawWindowControls(controlsOpen,feel,settingsPath,settingsMessage,
                           !mouseOnly,cameraActive,cameraStarting,cameraRequest);
        if (cameraRequest) cameraStarting=true;
        EndDrawing();
    }
};
Environment3D::Environment3D(const std::filesystem::path& directory,bool mouseOnly):impl_(std::make_unique<Impl>(directory,mouseOnly)) {}
Environment3D::~Environment3D()=default;
bool Environment3D::shouldClose() const {return WindowShouldClose()||impl_->exitRequested;}
void Environment3D::drawFrame(const Snapshot& snapshot) {impl_->drawFrame(snapshot);}
std::optional<int> Environment3D::takeCameraRequest() {
    auto request=impl_->cameraRequest;
    impl_->cameraRequest.reset();
    return request;
}
void Environment3D::setCameraStatus(bool active,std::string status) {
    impl_->cameraActive=active;
    impl_->cameraStarting=false;
    impl_->mouseInput=!active;
    impl_->settingsMessage=std::move(status);
}
}
