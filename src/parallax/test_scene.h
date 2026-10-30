#pragma once
#include <raylib.h>
#include "window_motion.h"
namespace tesseract {
// Fixed world geometry behind the screen plane, independent of tracking and UI.
class TestScene {
public:
    TestScene();
    ~TestScene();
    TestScene(const TestScene&) = delete;
    TestScene& operator=(const TestScene&) = delete;
    void draw(float roomWidth) const;
    void drawDepthWindow(float width,float height,const WindowFeel& settings,Vector3 eye) const;
private:
    Shader lighting_{};
    Shader sculptureLighting_{};
    Model sculpture_{};
    float sculptureExtent_ = 1;
    int eyeUniform_ = -1;
};
}
