#include <raylib.h> // Include the raylib library header file

using namespace std;

// Dimensions of the window
const int win_width = 600;
const int win_height = 400;

int main(){

    InitWindow(win_width, win_height, "Test window"); // Create the window instance
    SetTargetFPS(60); // 60 FPS

    int cube_x = 275;
    int increment = 5;

    while(!WindowShouldClose()){ // While the window remains open

        BeginDrawing();// Being drawing sequence
            ClearBackground(RAYWHITE); // Clear the background
            DrawRectangle(cube_x, 175, 50, 50, GREEN);
        EndDrawing(); // End drawing sequence

        // If the square has reached either side, change direction of increment
        if(cube_x == 0 || cube_x == 550){
            increment *= -1;
        }

        // Add increment
        cube_x += increment;
    }

    CloseWindow(); // Ensure that the window is closed
    return 0;
}