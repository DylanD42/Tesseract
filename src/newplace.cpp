#include <raylib.h> // Include the raylib library header file

using namespace std;

int main(){

    // Dimensions of the window
    const int win_width = 1000;
    const int win_height = 800;

    InitWindow(win_width, win_height, "3D holy"); // Create the window instance

    // Define the camera to look into our 3d world
    Camera3D camera = {};
    camera.position = Vector3{ 0.0f, 10.0f, 10.0f };     // Camera position (Move Camera's second value to 10.0 to see 3D and 0 to see 2D.)
    camera.target = Vector3{ 0.0f, 1.0f, 0.0f };        // Point that the camera looks at
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };            // Camera up vector (rotation towards target) {Don't know what that means}
    camera.fovy = 45.0f;                                // Camera field-of-view Y
    camera.projection = CAMERA_PERSPECTIVE;             // Camera mode type (Might change in future)

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

        BeginDrawing();
            
            ClearBackground(RAYWHITE);

            BeginMode3D(camera);

                DrawCube(cubePosition, 2.0f, 2.0f, 2.0f, RED);
                DrawCubeWires(cubePosition, 2.0f, 2.0f, 2.0f, MAROON);
                DrawSphere(spherePosition, 1.5f, BLUE);

                DrawGrid(10, 1.0f);

            EndMode3D();

            DrawText("Holy 3D environment", 10, 40, 20, DARKBLUE);

            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow(); // Ensure that the window is closed
    return 0;
}