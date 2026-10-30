#include "ui.h"
#include <raylib.h>
namespace tesseract::ui {
Actions pollActions() {return {IsKeyPressed(KEY_R),IsKeyPressed(KEY_F11)};}
void drawOverlay(const Snapshot& s,const std::array<float,3>& eye) {
    DrawFPS(10,10);
    DrawText("C/V settings | R center | M mouse | B room | F11 fullscreen | H hide HUD",10,42,17,DARKBLUE);
    DrawText(TextFormat("Depth mode | virtual eye X %.2f Y %.2f Z %.2f",eye[0],eye[1],eye[2]),10,67,17,DARKGRAY);
    DrawText(TextFormat("%s | faces %d | score %.2f | center %.3f, %.3f",
        s.status.c_str(),s.faceCount,s.faceScore,s.faceCenter[0],s.faceCenter[1]),10,92,16,DARKGRAY);
}
}
