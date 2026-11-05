#include <raylib.h> // Include the raylib library header file
#include "../scenes/project_scenes.h"

using namespace std;

// Dimensions of the window
const int win_width = 600;
const int win_height = 400;
//this is a git test
int main(){

    InitWindow(win_width, win_height, "Test window"); // Create the window instance
    SetTargetFPS(60); // 60 FPS

    while(!WindowShouldClose()){ // While the window remains open

        BeginDrawing();// Being drawing sequence
            ClearBackground(RAYWHITE); // Clear the background
            tesseract::drawAnimatedSquare2D(GetScreenWidth(),GetScreenHeight(),GetTime());
        EndDrawing(); // End drawing sequence
    }

    CloseWindow(); // Ensure that the window is closed
    return 0;
}