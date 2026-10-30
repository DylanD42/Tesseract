#pragma once
#include <raylib.h>

namespace tesseract {
struct ScreenBounds { double left, right, bottom, top, planeZ; };
struct OffAxisCamera { Matrix view; Matrix projection; };
ScreenBounds depthWindowBounds(double pixelWidth,double pixelHeight,double openingWidth,
                               double centerY,double planeZ);
// Camera rotation stays aligned to the screen. Only the eye position moves.
OffAxisCamera makeOffAxisCamera(ScreenBounds screen, Vector3 eye,
                                double nearPlane = 0.05, double farPlane = 180);
}
