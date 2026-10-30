#include "test_scene.h"
#include "settings.h"
#include <algorithm>
#include <cmath>

namespace tesseract {
namespace {
const Color warm{214,189,151,255}, cool{118,149,173,255}, wall{184,194,201,255};
const Color dark{39,53,69,255}, teal{49,185,174,255}, amber{240,165,61,255};
constexpr float floorY=settings::kScreenCenterY-settings::kScreenHeightUnits/2;
void shadow(float x,float z,float radius) {
    // Soft-looking concentric contact patches; deliberately inexpensive, not dynamic shadow maps.
    for (int i=3;i>=0;--i)
        DrawCylinder({x,floorY+0.012f+(3-i)*0.002f,z},radius*(0.75f+i*0.16f),radius*(0.75f+i*0.16f),
                     0.002f,48,Fade(BLACK,0.045f+(3-i)*0.018f));
}
void pedestal(float x,float z,float height,Color color) {
    shadow(x,z,0.95f);
    DrawCube({x,floorY+height/2,z},1.5f,height,1.5f,color);
    DrawCubeWires({x,floorY+height/2,z},1.5f,height,1.5f,Fade(dark,0.5f));
}
}
TestScene::TestScene() {
    const char* vs=R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 normal;
out vec3 world;
out vec4 tint;
void main() {
    normal=vertexNormal;
    world=vertexPosition;
    tint=vertexColor;
    gl_Position=mvp*vec4(vertexPosition,1.0);
})";
    const char* fs=R"(#version 330
in vec3 normal;
in vec3 world;
in vec4 tint;
out vec4 finalColor;
void main() {
    vec3 n=normalize(normal);
    float light=0.42+0.58*max(dot(n,normalize(vec3(-0.45,1.0,0.6))),0.0);
    vec3 color=tint.rgb*light;
    float fog=clamp((-world.z-3.0)/65.0,0.0,0.27);
    finalColor=vec4(mix(color,vec3(0.18,0.23,0.29),fog),tint.a);
})";
    lighting_=LoadShaderFromMemory(vs,fs);
    const char* sculptureVS=R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 position;
out vec3 normal;
void main() {
    position=(matModel*vec4(vertexPosition,1.0)).xyz;
    normal=mat3(matNormal)*vertexNormal;
    gl_Position=mvp*vec4(vertexPosition,1.0);
})";
    const char* sculptureFS=R"(#version 330
in vec3 position;
in vec3 normal;
uniform vec4 colDiffuse;
uniform vec3 eyePosition;
out vec4 finalColor;
void main() {
    vec3 n=normalize(normal);
    vec3 key=normalize(vec3(-0.4,0.8,0.65));
    vec3 fill=normalize(vec3(0.7,0.1,-0.5));
    vec3 view=normalize(eyePosition-position);
    float diffuse=0.28+0.62*max(dot(n,key),0.0)+0.22*max(dot(n,fill),0.0);
    float specular=0.26*pow(max(dot(n,normalize(key+view)),0.0),48.0);
    float rim=0.12*pow(1.0-max(dot(n,view),0.0),3.0);
    finalColor=vec4(colDiffuse.rgb*diffuse+vec3(specular)+vec3(0.25,0.45,0.55)*rim,colDiffuse.a);
})";
    sculptureLighting_=LoadShaderFromMemory(sculptureVS,sculptureFS);
    eyeUniform_=GetShaderLocation(sculptureLighting_,"eyePosition");
    sculpture_=LoadModelFromMesh(GenMeshKnot(1.0f,0.3f,128,24));
    for (int i=0;i<sculpture_.materialCount;++i) sculpture_.materials[i].shader=sculptureLighting_;
    const auto bounds=GetModelBoundingBox(sculpture_);
    sculptureExtent_=std::max({bounds.max.x-bounds.min.x,bounds.max.y-bounds.min.y,bounds.max.z-bounds.min.z,0.001f});
}
TestScene::~TestScene() {
    if (sculpture_.meshCount) UnloadModel(sculpture_);
    if (sculptureLighting_.id) UnloadShader(sculptureLighting_);
    if (lighting_.id) UnloadShader(lighting_);
}
void TestScene::draw(float roomWidth) const {
    const float half=roomWidth/2;
    const float ceiling=floorY+10;
    if (lighting_.id) BeginShaderMode(lighting_);
    // A continuous tiled floor, solid walls and ceiling establish scale and perspective.
    DrawCube({0,floorY-0.14f,-10.5f},roomWidth,0.25f,23,wall);
    const int columns=std::max(2,static_cast<int>(std::ceil(roomWidth/1.5f)));
    const float tileWidth=roomWidth/columns;
    for (int z=0;z<15;++z) for (int x=0;x<columns;++x) {
        const float px=-half+(x+0.5f)*tileWidth;
        DrawCube({px,floorY,-21.5f+(z+0.5f)*1.5f},tileWidth-0.025f,0.018f,1.475f,
                 (x+z)%2?Color{166,180,187,255}:Color{217,221,211,255});
    }
    DrawCube({-half-0.12f,1,-10.5f},0.24f,10,23,cool);
    DrawCube({ half+0.12f,1,-10.5f},0.24f,10,23,warm);
    DrawCube({0,ceiling+0.12f,-10.5f},roomWidth,0.24f,23,Color{211,218,216,255});
    DrawCube({0,1,-22},roomWidth,10,0.25f,Color{107,130,153,255});
    // Doorway in a partition: moving laterally reveals previously hidden back-room detail.
    const float opening=std::min(4.0f,roomWidth*0.5f);
    const float side=(roomWidth-opening)/2;
    DrawCube({-(opening+side)/2,1,-11},side,10,0.3f,wall);
    DrawCube({ (opening+side)/2,1,-11},side,10,0.3f,wall);
    DrawCube({0,4.75f,-11},opening,2.5f,0.3f,wall);
    DrawCube({-opening/2-0.12f,-0.25f,-10.8f},0.24f,7.5f,0.35f,teal);
    DrawCube({ opening/2+0.12f,-0.25f,-10.8f},0.24f,7.5f,0.35f,teal);
    DrawCube({0,3.6f,-10.8f},opening+0.5f,0.25f,0.35f,teal);
    // Repeated posts, wall bands and ceiling beams make small viewpoint shifts visible.
    for (int i=0;i<4;++i) {
        const float z=-2.5f-i*5;
        for (int sign:{-1,1}) {
            DrawCube({sign*(half-0.3f),0.25f,z},0.38f,8.5f,0.38f,dark);
            DrawCube({sign*(half-0.06f),-2.4f,z-1.8f},0.09f,0.13f,3.8f,teal);
        }
        DrawCube({0,5.45f,z},roomWidth,0.35f,0.4f,dark);
        DrawCube({0,5.24f,z+0.05f},roomWidth*0.5f,0.035f,0.12f,Color{255,236,183,255});
    }
    // Three different depths and overlapping silhouettes, all behind the display.
    pedestal(-half*0.47f,-2.5f,1.15f,Color{196,205,206,255});
    DrawSphere({-half*0.47f,floorY+2.05f,-2.5f},0.9f,amber);
    pedestal(half*0.40f,-6.3f,1.3f,Color{190,198,201,255});
    DrawCube({half*0.40f,floorY+2.15f,-6.3f},1.7f,1.7f,1.7f,teal);
    DrawCubeWires({half*0.40f,floorY+2.15f,-6.3f},1.71f,1.71f,1.71f,dark);
    pedestal(0,-17.3f,1.0f,Color{175,189,197,255});
    DrawSphere({0,floorY+2.1f,-17.3f},1.1f,Color{219,111,95,255});
    // Rear calibration target and frame serve as a visual anchor behind the doorway.
    DrawCube({0,0.6f,-21.8f},3.5f,3.5f,0.1f,dark);
    DrawCube({0,0.6f,-21.72f},3.1f,3.1f,0.04f,Color{226,230,217,255});
    DrawCube({0,0.6f,-21.68f},0.07f,3.1f,0.02f,teal);
    DrawCube({0,0.6f,-21.68f},3.1f,0.07f,0.02f,teal);
    if (lighting_.id) EndShaderMode();
    // Thin dark aperture follows the actual screen plane; the room stays fixed behind it.
    DrawCube({-half-0.04f,1,settings::kScreenPlaneZ-0.1f},0.08f,10,0.2f,dark);
    DrawCube({ half+0.04f,1,settings::kScreenPlaneZ-0.1f},0.08f,10,0.2f,dark);
    DrawCube({0,floorY-0.04f,settings::kScreenPlaneZ-0.1f},roomWidth,0.08f,0.2f,dark);
    DrawCube({0,ceiling+0.04f,settings::kScreenPlaneZ-0.1f},roomWidth,0.08f,0.2f,dark);
}
void TestScene::drawDepthWindow(float width,float height,const WindowFeel& feel,Vector3 eye) const {
    const float halfW=width/2,halfH=height/2;
    const float bottom=settings::kScreenCenterY-halfH,top=settings::kScreenCenterY+halfH;
    const float front=settings::kScreenPlaneZ;
    const float depth=width*static_cast<float>(feel.roomDepth)/3;
    const float back=front-depth;
    const Color grid=Fade(Color{60,191,212,255},static_cast<float>(feel.gridOpacity));
    // Five screen-aligned grids converge through the actual opening, rather than
    // camera-facing grids or a moving room. No geometry occupies the front face.
    for (int i=0;i<=feel.gridDensity;++i) {
        const float t=i/static_cast<float>(feel.gridDensity);
        const float x=-halfW+width*t,y=bottom+height*t,z=front-depth*t;
        DrawLine3D({x,bottom,front},{x,bottom,back},grid);
        DrawLine3D({x,top,front},{x,top,back},grid);
        DrawLine3D({-halfW,y,front},{-halfW,y,back},grid);
        DrawLine3D({halfW,y,front},{halfW,y,back},grid);
        DrawLine3D({-halfW,bottom,z},{halfW,bottom,z},grid);
        DrawLine3D({-halfW,top,z},{halfW,top,z},grid);
        DrawLine3D({-halfW,bottom,z},{-halfW,top,z},grid);
        DrawLine3D({halfW,bottom,z},{halfW,top,z},grid);
        DrawLine3D({x,bottom,back},{x,top,back},grid);
        DrawLine3D({-halfW,y,back},{halfW,y,back},grid);
    }
    const Color edge{94,213,225,255};
    DrawLine3D({-halfW,bottom,front},{halfW,bottom,front},edge);
    DrawLine3D({halfW,bottom,front},{halfW,top,front},edge);
    DrawLine3D({halfW,top,front},{-halfW,top,front},edge);
    DrawLine3D({-halfW,top,front},{-halfW,bottom,front},edge);
    const float eyeData[]={eye.x,eye.y,eye.z};
    SetShaderValue(sculptureLighting_,eyeUniform_,eyeData,SHADER_UNIFORM_VEC3);
    for (int i=0;i<3;++i) {
        const float fraction=0.15f+0.35f*i;
        const float size=width*(0.22f-0.03f*i)/sculptureExtent_;
        const Vector3 position{(i-1)*width*0.20f,settings::kScreenCenterY,front-depth*fraction};
        DrawModelEx(sculpture_,position,{0,1,0},15.0f*(i-1),{size,size,size},Color{205,215,223,255});
    }
}

}
