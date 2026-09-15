#include <raylib.h> // Include the raylib library header file

using namespace std;

// This state can later be updated by a head tracker instead of the keyboard.
struct CameraRig {
    Vector3 position;
    Vector3 target; // A point in the direction the camera is looking.
    Vector3 up;     // Defines which direction appears as up on screen.
};

void UpdateKeyboardCamera(CameraRig& rig, float deltaTime) {
    const float moveSpeed = 5.0f;
    const float distance = moveSpeed * deltaTime;
    Vector3 movement = {};

    // Movement is scaled by frame time, so speed stays consistent across frame rates.
    if (IsKeyDown(KEY_W)) movement.z -= distance;
    if (IsKeyDown(KEY_S)) movement.z += distance;
    if (IsKeyDown(KEY_A)) movement.x -= distance;
    if (IsKeyDown(KEY_D)) movement.x += distance;
    if (IsKeyDown(KEY_SPACE)) movement.y += distance;
    if (IsKeyDown(KEY_LEFT_CONTROL)) movement.y -= distance;

    // Move the look-at point too, keeping the camera pointed in the same direction.
    rig.position.x += movement.x;
    rig.position.y += movement.y;
    rig.position.z += movement.z;
    rig.target.x += movement.x;
    rig.target.y += movement.y;
    rig.target.z += movement.z;
}

Camera3D CreateCamera(const CameraRig& rig) {
    Camera3D camera = {};
    camera.position = rig.position;
    camera.target = rig.target;
    camera.up = rig.up;
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

int main(){

    // Dimensions of the window
    const int win_width = 1000;
    const int win_height = 800;

    InitWindow(win_width, win_height, "3D holy"); // Create the window instance

    CameraRig cameraRig = {
        Vector3{ 0.0f, 1.0f, 10.0f },
        Vector3{ 0.0f, 1.0f, 0.0f },
        Vector3{ 0.0f, 1.0f, 0.0f }
    };

    Vector3 cubePosition = { 0.0f, 0.0f, 0.0f };
    Vector3 spherePosition = { 4.0f, 0.0f, 0.0f};

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
        // Input updates the rig, then the rig becomes Raylib's render camera.
        UpdateKeyboardCamera(cameraRig, GetFrameTime());
        Camera3D camera = CreateCamera(cameraRig);

        BeginDrawing();
            
            ClearBackground(RAYWHITE);

            BeginMode3D(camera);

                DrawCube(cubePosition, 2.0f, 2.0f, 2.0f, RED);
                DrawCubeWires(cubePosition, 2.0f, 2.0f, 2.0f, MAROON);
                DrawSphere(spherePosition, 1.5f, BLUE);

                DrawGrid(10, 1.0f);

            EndMode3D();

            DrawText("Holy 3D environment", 10, 40, 20, DARKBLUE);
            DrawText("WASD: move   Space/Ctrl: up/down", 10, 65, 20, DARKGRAY);

            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow(); // Ensure that the window is closed
    return 0;
}