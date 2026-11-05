#include "project_scenes.h"
#include <algorithm>
#include <cmath>

namespace tesseract {
namespace {
enum class ShapeType { Cube,Sphere,Cylinder,Cone,Capsule };

struct FloatingShape {
    ShapeType type;
    Vector3 position;
    float size;
    Color color;
};

constexpr FloatingShape kFloatingShapes[] = {
    {ShapeType::Cube,     {-4.2f,  3.4f, -1.5f}, 1.0f, RED},
    {ShapeType::Sphere,   {-1.8f,  4.0f,  2.0f}, 1.2f, BLUE},
    {ShapeType::Cylinder, { 2.1f,  3.5f, -4.0f}, 1.4f, GREEN},
    {ShapeType::Cone,     { 4.6f,  2.3f,  3.3f}, 1.2f, ORANGE},
    {ShapeType::Capsule,  {-5.0f,  0.1f,  1.0f}, 1.3f, PURPLE},
    {ShapeType::Sphere,   {-2.6f,  0.4f,  6.4f}, 1.1f, GOLD},
    {ShapeType::Cube,     { 0.2f,  1.6f, -6.0f}, 1.3f, SKYBLUE},
    {ShapeType::Cylinder, { 3.4f,  0.2f,  0.0f}, 1.5f, PINK},
    {ShapeType::Cone,     { 5.0f, -2.1f,  1.8f}, 1.4f, LIME},
    {ShapeType::Capsule,  { 1.4f, -2.6f,  6.6f}, 1.2f, VIOLET},
    {ShapeType::Cube,     {-1.8f, -3.0f, -2.5f}, 1.1f, MAROON},
    {ShapeType::Sphere,   {-4.5f, -2.0f, -5.0f}, 1.3f, DARKBLUE},
    {ShapeType::Cylinder, { 2.7f, -3.4f, -5.5f}, 1.3f, DARKGREEN},
    {ShapeType::Cone,     { 0.2f,  4.1f,  6.8f}, 1.0f, BROWN},
    {ShapeType::Capsule,  { 4.0f, -0.9f, -3.0f}, 1.4f, DARKPURPLE}
};

void drawFloatingShape(const FloatingShape& shape,float scale,float screenPlaneZ) {
    const Vector3 position{
        shape.position.x*scale,
        1.0f+(shape.position.y-1.0f)*scale,
        screenPlaneZ+(shape.position.z-5.0f)*scale
    };
    const float size=shape.size*scale;
    switch (shape.type) {
    case ShapeType::Cube:
        DrawCube(position,size,size,size,shape.color);
        break;
    case ShapeType::Sphere:
        DrawSphere(position,size*0.5f,shape.color);
        break;
    case ShapeType::Cylinder:
        DrawCylinder(position,size*0.35f,size*0.35f,size,12,shape.color);
        break;
    case ShapeType::Cone:
        DrawCylinder(position,0.0f,size*0.45f,size,12,shape.color);
        break;
    case ShapeType::Capsule:
        DrawCapsule({position.x,position.y-size*0.3f,position.z},
                    {position.x,position.y+size*0.3f,position.z},
                    size*0.28f,8,8,shape.color);
        break;
    }
}

float animatedSquareLeftFraction(double elapsedSeconds) {
    constexpr float squareFraction=50.0f/600.0f;
    constexpr float initialLeftFraction=275.0f/600.0f;
    constexpr float speedFractionPerSecond=0.5f;
    const float travel=1.0f-squareFraction;
    const float period=2.0f*travel;
    const float phase=std::fmod(initialLeftFraction+speedFractionPerSecond*
                                static_cast<float>(std::max(0.0,elapsedSeconds)),period);
    return phase<=travel?phase:period-phase;
}
}

void drawFloatingShapeScene(float width,float height,float screenPlaneZ,float scale) {
    for (const auto& shape:kFloatingShapes) drawFloatingShape(shape,scale,screenPlaneZ);
    const float half=width/2.0f;
    const float bottom=1.0f-height/2.0f,top=1.0f+height/2.0f;
    DrawLine3D({-half,bottom,screenPlaneZ},{half,bottom,screenPlaneZ},DARKGRAY);
    DrawLine3D({half,bottom,screenPlaneZ},{half,top,screenPlaneZ},DARKGRAY);
    DrawLine3D({half,top,screenPlaneZ},{-half,top,screenPlaneZ},DARKGRAY);
    DrawLine3D({-half,top,screenPlaneZ},{-half,bottom,screenPlaneZ},DARKGRAY);
}

void drawAnimatedSquareScene(float width,float screenCenterY,float screenPlaneZ,
                             double elapsedSeconds) {
    const float size=width*(50.0f/600.0f);
    const float left=animatedSquareLeftFraction(elapsedSeconds);
    const float centerX=-width/2.0f+width*(left+25.0f/600.0f);
    const float depth=std::max(1.0f,width*0.1f);
    DrawCube({centerX,screenCenterY,screenPlaneZ-depth},size,size,depth*0.025f,GREEN);
}

void drawAnimatedSquare2D(int screenWidth,int screenHeight,double elapsedSeconds) {
    constexpr float squareFraction=50.0f/600.0f;
    const int size=std::max(1,static_cast<int>(screenWidth*squareFraction));
    const float leftFraction=animatedSquareLeftFraction(elapsedSeconds);
    const int x=static_cast<int>(leftFraction*screenWidth);
    const int y=(screenHeight-size)/2;
    DrawRectangle(x,y,size,size,GREEN);
}
}
