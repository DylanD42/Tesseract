#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "../scenes/project_scenes.h"

using namespace std;

// The mouse currently stands in for viewer tracking.
struct ViewerPose {
    Vector3 position;
    Vector3 target;
    Vector3 up;
};

void UpdateViewerPoseFromInput(ViewerPose& viewer, float screenWidth, float screenHeight, float screenPlaneZ) {
    const Vector2 mouse = GetMousePosition();
    // Turn the mouse position into a small left/right and up/down viewer offset.
    const float offsetX = (mouse.x / GetScreenWidth() - 0.5f) * screenWidth * 0.24f;
    const float offsetY = (0.5f - mouse.y / GetScreenHeight()) * screenHeight * 0.24f;
    // W/S fakes the viewer leaning closer to or farther from the display.
    const float depthStep = 2.0f * GetFrameTime();

    viewer.position.x = offsetX;
    viewer.position.y = 1.0f + offsetY;
    if (IsKeyDown(KEY_W)) viewer.position.z -= depthStep;
    if (IsKeyDown(KEY_S)) viewer.position.z += depthStep;
    // Don't let the viewer go through the screen or the object popping out of it.
    if (viewer.position.z < screenPlaneZ + 4.0f) viewer.position.z = screenPlaneZ + 4.0f;
    if (viewer.position.z > screenPlaneZ + 6.0f) viewer.position.z = screenPlaneZ + 6.0f;
    // Keep looking straight ahead while the viewer position changes.
    viewer.target = Vector3{ viewer.position.x, viewer.position.y, viewer.position.z - 1.0f };
}

Camera3D CreateCamera(const ViewerPose& viewer) {
    Camera3D camera = {};
    camera.position = viewer.position;
    camera.target = viewer.target;
    camera.up = viewer.up;
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

void ApplyOffAxisProjection(const ViewerPose& viewer, float screenWidth, float screenHeight,
                            float screenDistance, float nearPlane, float farPlane) {
    // Work out where the screen edges land on the near plane from this viewer position.
    const float left = nearPlane * (-screenWidth * 0.5f - viewer.position.x) / screenDistance;
    const float right = nearPlane * (screenWidth * 0.5f - viewer.position.x) / screenDistance;
    const float bottom = nearPlane * (-screenHeight * 0.5f - (viewer.position.y - 1.0f)) / screenDistance;
    const float top = nearPlane * (screenHeight * 0.5f - (viewer.position.y - 1.0f)) / screenDistance;

    rlSetMatrixProjection(MatrixFrustum(left, right, bottom, top, nearPlane, farPlane));
}

int main(){

    // Dimensions of the window
    const int win_width = 1000;
    const int win_height = 800;
    const float screenHeight = 10.0f;
    const float screenWidth = screenHeight * win_width / win_height;
    // The screen is an imaginary window in the 3D scene, not a drawn surface.
    const float screenPlaneZ = 5.0f;
    const float nearPlane = 0.01f;

    InitWindow(win_width, win_height, "3D holy"); // Create the window instance
    SetMousePosition(win_width / 2, win_height / 2);

    ViewerPose viewerPose = {
        Vector3{ 0.0f, 1.0f, 10.0f },
        Vector3{ 0.0f, 1.0f, 9.0f },
        Vector3{ 0.0f, 1.0f, 0.0f }
    };

    // Positive z is closer to us, so a few shapes can poke through the window.
    SetTargetFPS(60);               // Set our game to run at 60 frames-per-second

    /*
    //Camera define!
    Camera3D camera = { 0 };                        //So Something Was going wrong with position/target/up when they had the (Vector3) expression infront of them, Error said "Expecting Expression", so I just got rid of (Vector3) and it should work??? But the raylib example uses (Vector3) ... idk brah
    camera.position = (Vector3){ 0.0f, 10.0f, 10.0f };       //Position
    camera.target = (Vector3){ 0.0f, 10.0f, 10.0f };         //Where Camera Looks
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };               //Camera Up Vector (rotation towards target) {Idk what this means}
    camera.fovy = 45.0f;                            //Camera field-of-view Y ( So how far camera up and down is I assume)
    camera.projection = CAMERA_PERSPECTIVE;         //Camera mode type ( Might change in future )

    Vector3 cubePosition = { 0.0f, 0.0f, 0.0f};
    SetTargetFPS(60); // 60 FPS
    */

    while(!WindowShouldClose()) // While the window remains open
    {
        UpdateViewerPoseFromInput(viewerPose, screenWidth, screenHeight, screenPlaneZ);
        Camera3D camera = CreateCamera(viewerPose);

        BeginDrawing();
            
            ClearBackground(RAYWHITE);

            BeginMode3D(camera);
                // Replace the normal perspective with the viewer-aligned window projection.
                ApplyOffAxisProjection(viewerPose, screenWidth, screenHeight,
                                       viewerPose.position.z - screenPlaneZ, nearPlane, 100.0f);

                tesseract::drawFloatingShapeScene(screenWidth,screenHeight,screenPlaneZ);

            EndMode3D();

            DrawText("Off-axis window prototype", 10, 40, 20, DARKBLUE);
            DrawText("Mouse: move viewer left/right/up/down   W/S: closer/farther", 10, 65, 20, DARKGRAY);

            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow(); // Ensure that the window is closed
    return 0;
}