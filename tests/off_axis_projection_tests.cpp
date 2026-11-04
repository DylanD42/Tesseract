#include "off_axis_projection.h"
#include <raymath.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace tesseract;
namespace {
void require(bool result,const char* message) { if (!result) throw std::runtime_error(message); }
void near(double a,double b,const char* message) {require(std::abs(a-b)<2e-5,message);}
Vector2 project(Vector3 world,const OffAxisCamera& camera) {
    Quaternion point{world.x,world.y,world.z,1};
    point=QuaternionTransform(QuaternionTransform(point,camera.view),camera.projection);
    return {point.x/point.w,point.y/point.w};
}
void cornersStayFixed(ScreenBounds s,Vector3 eye) {
    const auto camera=makeOffAxisCamera(s,eye);
    const Vector3 corners[]={{static_cast<float>(s.left),static_cast<float>(s.bottom),static_cast<float>(s.planeZ)},
                             {static_cast<float>(s.right),static_cast<float>(s.bottom),static_cast<float>(s.planeZ)},
                             {static_cast<float>(s.right),static_cast<float>(s.top),static_cast<float>(s.planeZ)},
                             {static_cast<float>(s.left),static_cast<float>(s.top),static_cast<float>(s.planeZ)}};
    const Vector2 expected[]={{-1,-1},{1,-1},{1,1},{-1,1}};
    for (int i=0;i<4;++i) {
        const auto ndc=project(corners[i],camera);
        near(ndc.x,expected[i].x,"Screen-plane corners must stay attached to viewport edges in X");
        near(ndc.y,expected[i].y,"Screen-plane corners must stay attached to viewport edges in Y");
    }
}
}
int main() {
    try {
        const auto full=depthWindowBounds(1280,720,30,1,1);
        near(full.left,-15,"Opening width must be independent of monitor measurements");
        near(full.top-full.bottom,30.0*720/1280,"Opening must follow viewport aspect ratio");
        for (Vector3 eye:{Vector3{0,1,51},Vector3{6,3,12},Vector3{-4,-2,9},Vector3{15,10,6}})
            cornersStayFixed(full,eye);
        const auto portrait=depthWindowBounds(720,1280,30,1,1);
        near(portrait.right-portrait.left,30,"Resizing must keep virtual opening width stable");
        near(portrait.top-portrait.bottom,30.0*1280/720,"Portrait viewport must update the opening height");
        cornersStayFixed(portrait,{3,2,51});
        const ScreenBounds screen{-8,8,-4,6,1};
        const auto centered=makeOffAxisCamera(screen,{0,1,21});
        const auto right=makeOffAxisCamera(screen,{2,1,21});
        const auto above=makeOffAxisCamera(screen,{0,3,21});
        const auto close=makeOffAxisCamera(screen,{0,1,11});
        const auto behind=project({0,1,-19},right);
        require(behind.x>project({0,1,-19},centered).x,
                "An object behind the panel must shift right on screen when the eye moves right");
        require(project({0,1,11},right).x<0,
                "An object in front of the panel must shift opposite to eye movement");
        require(project({0,1,-19},above).y>0,
                "Vertical parallax must use the same sign as horizontal parallax");
        require(project({4,1,-19},close).x<project({4,1,-19},centered).x,
                "Moving closer to the window must reveal a wider section of the room");
        const auto world=Vector3{3,2,-9};
        const auto projected=project(world,right);
        const double depth=screen.planeZ-world.z;
        const double distance=21-screen.planeZ;
        const double intersectionX=2+(world.x-2)*distance/(distance+depth);
        const double expectedX=2*(intersectionX-screen.left)/(screen.right-screen.left)-1;
        near(projected.x,expectedX,"Projection must agree with an independent eye-to-object ray/screen intersection");
        const auto rescaled=makeOffAxisCamera({-800,800,-400,600,100},{200,100,2100},5,18000);
        const auto scaled=project({300,200,-900},rescaled);
        near(scaled.x,projected.x,"Uniform physical unit conversion must preserve the image");
        near(scaled.y,projected.y,"Uniform physical unit conversion must preserve vertical perspective");
        bool rejected=false;
        try {makeOffAxisCamera(screen,{0,0,0});} catch (const std::invalid_argument&) {rejected=true;}
        require(rejected,"Eye behind the screen must be rejected rather than reversing the frustum");
        std::cout << "Off-axis projection tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
