#include "window_controls.h"
#include <raylib.h>
#include <algorithm>
#include <exception>

namespace tesseract {
namespace {
const Color ink{230,236,244,255},muted{151,167,187,255},accent{79,205,211,255};
bool button(Rectangle r,const char* label,bool enabled=true) {
    const bool hover=CheckCollisionPointRec(GetMousePosition(),r);
    DrawRectangleRounded(r,0.18f,8,hover&&enabled?Color{51,82,101,255}:Color{34,52,72,255});
    DrawText(label,static_cast<int>(r.x+12),static_cast<int>(r.y+10),16,ink);
    return enabled&&hover&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
void slider(float x,float y,const char* label,double& value,double low,double high,const char* format) {
    DrawText(label,static_cast<int>(x),static_cast<int>(y),16,ink);
    DrawText(TextFormat(format,value),static_cast<int>(x+381),static_cast<int>(y),16,accent);
    const Rectangle track{x,y+31,440,8};
    if ((IsMouseButtonDown(MOUSE_BUTTON_LEFT)||IsMouseButtonPressed(MOUSE_BUTTON_LEFT))&&
        CheckCollisionPointRec(GetMousePosition(),{track.x-8,track.y-12,track.width+16,32}))
        value=low+(high-low)*std::clamp((GetMouseX()-track.x)/track.width,0.0f,1.0f);
    const float amount=static_cast<float>((value-low)/(high-low));
    DrawRectangleRounded(track,1,8,Color{43,59,79,255});
    DrawRectangleRounded({track.x,track.y,track.width*amount,track.height},1,8,accent);
    DrawCircle(static_cast<int>(track.x+track.width*amount),static_cast<int>(track.y+4),8,ink);
}
}
void drawWindowControls(bool& open,WindowFeel& s,const std::filesystem::path& path,std::string& message,
                        bool cameraAvailable,bool cameraActive,bool cameraStarting,
                        std::optional<int>& cameraRequest) {
    if (!open) return;
    DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),Fade(BLACK,0.65f));
    const float x=GetScreenWidth()/2.0f-300,y=GetScreenHeight()/2.0f-260;
    DrawRectangleRounded({x,y,600,520},0.04f,10,Color{17,24,36,255});
    DrawText("TESSERACT SETTINGS",static_cast<int>(x+38),static_cast<int>(y+28),25,ink);
    DrawText("Tracking sensitivity and camera input.",static_cast<int>(x+38),static_cast<int>(y+65),14,muted);
    slider(x+38,y+107,"Sensitivity",s.sensitivity,0.2,3,"%.1f");
    slider(x+38,y+175,"Smoothing",s.smoothing,0.3,0.95,"%.2f");

    DrawText("CAMERA INPUT",static_cast<int>(x+38),static_cast<int>(y+253),16,ink);
    if (!cameraAvailable) {
        DrawText("Mouse preview is active for this launch.",static_cast<int>(x+38),static_cast<int>(y+279),14,muted);
    } else {
        DrawText("Device index",static_cast<int>(x+38),static_cast<int>(y+286),14,muted);
        if (button({x+194,y+276,40,36},"-",!cameraActive&&!cameraStarting&&s.cameraIndex>0))
            s.cameraIndex=std::max(0,s.cameraIndex-1);
        DrawText(TextFormat("%d",s.cameraIndex),static_cast<int>(x+252),static_cast<int>(y+283),18,accent);
        if (button({x+284,y+276,40,36},"+",!cameraActive&&!cameraStarting&&s.cameraIndex<9))
            s.cameraIndex=std::min(9,s.cameraIndex+1);
        const char* cameraButton=cameraStarting?"Starting...":cameraActive?"Stop camera":"Start camera";
        if (button({x+338,y+276,140,36},cameraButton,!cameraStarting)) {
            cameraRequest=cameraActive?-1:s.cameraIndex;
            message=cameraActive?"Stopping camera...":"Opening selected camera...";
        }
    }

    if (button({x+38,y+350,132,38},"Save settings")) {
        try {saveWindowFeel(path,s);message="View settings saved";}
        catch (const std::exception& e) {message=e.what();}
    }
    if (button({x+184,y+350,132,38},"Reset defaults")) {
        const int activeCameraIndex=s.cameraIndex;
        s=WindowFeel{};
        if (cameraActive||cameraStarting) s.cameraIndex=activeCameraIndex;
        message="Defaults applied; Save to keep them";
    }
    if (button({x+330,y+350,148,38},"Back")) open=false;
    DrawText("R center | M mouse preview | H diagnostics",static_cast<int>(x+38),static_cast<int>(y+415),14,muted);
    BeginScissorMode(static_cast<int>(x+38),static_cast<int>(y+451),440,25);
    DrawText(message.c_str(),static_cast<int>(x+38),static_cast<int>(y+455),13,accent);
    EndScissorMode();
}
}
