#pragma once

#include <raylib.h>

namespace tesseract {
enum class ProjectScene {
    FloatingShapes,
    AnimatedSquare
};

void drawFloatingShapeScene(float width,float height,float screenPlaneZ,float scale=1.0f);
void drawAnimatedSquareScene(float width,float screenCenterY,float screenPlaneZ,
                             double elapsedSeconds);
void drawAnimatedSquare2D(int screenWidth,int screenHeight,double elapsedSeconds);
}
