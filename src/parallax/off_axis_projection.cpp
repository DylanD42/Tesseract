#include "off_axis_projection.h"
#include <raymath.h>
#include <cmath>
#include <stdexcept>

namespace tesseract {
namespace {
void positive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0) throw std::invalid_argument(name);
}
}
ScreenBounds depthWindowBounds(double pixelWidth,double pixelHeight,double openingWidth,
                               double centerY,double planeZ) {
    positive(pixelWidth,"Viewport width must be positive");
    positive(pixelHeight,"Viewport height must be positive");
    positive(openingWidth,"Opening width must be positive");
    const double height=openingWidth*pixelHeight/pixelWidth;
    return {-openingWidth/2,openingWidth/2,centerY-height/2,centerY+height/2,planeZ};
}
OffAxisCamera makeOffAxisCamera(ScreenBounds s, Vector3 eye, double nearPlane, double farPlane) {
    positive(s.right-s.left, "Screen width must be positive");
    positive(s.top-s.bottom, "Screen height must be positive");
    positive(nearPlane, "Near plane must be positive");
    positive(farPlane-nearPlane, "Far plane must be beyond the near plane");
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z) ||
        !std::isfinite(s.left) || !std::isfinite(s.bottom) || !std::isfinite(s.planeZ))
        throw std::invalid_argument("Screen and eye positions must be finite");
    const double distance=eye.z-s.planeZ;
    positive(distance, "Eye must be in front of the screen");
    // Same screen-fixed construction as Magic-Window's MagicWindow.LateUpdate:
    // bounds relative to the absolute eye, scaled by near / distance-to-screen.
    const double scale=nearPlane/distance;
    return {MatrixTranslate(-eye.x,-eye.y,-eye.z),
            MatrixFrustum((s.left-eye.x)*scale,(s.right-eye.x)*scale,
                          (s.bottom-eye.y)*scale,(s.top-eye.y)*scale,nearPlane,farPlane)};
}
}
