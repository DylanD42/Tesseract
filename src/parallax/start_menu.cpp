#include "start_menu.h"
#include <raylib.h>
#include <algorithm>

namespace tesseract::ui {
namespace {
const Color ink{230,236,244,255};
const Color muted{151,167,187,255};
const Color accent{79,205,211,255};

MenuAction button(Rectangle bounds,const char* label,MenuAction action,bool interactive) {
    const bool hover=CheckCollisionPointRec(GetMousePosition(),bounds);
    const Color fill=hover&&interactive?Color{51,82,101,255}:Color{34,52,72,255};
    DrawRectangleRounded(bounds,0.16f,10,fill);
    const int fontSize=20;
    const int textWidth=MeasureText(label,fontSize);
    DrawText(label,static_cast<int>(bounds.x+(bounds.width-textWidth)/2),
             static_cast<int>(bounds.y+(bounds.height-fontSize)/2),fontSize,ink);
    DrawRectangleRoundedLines(bounds,0.16f,10,hover&&interactive?accent:Color{61,79,99,255});
    return hover&&interactive&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)?action:MenuAction::None;
}

void drawBackground() {
    const int width=GetScreenWidth();
    const int height=GetScreenHeight();
    DrawRectangleGradientV(0,0,width,height,Color{13,22,37,255},Color{5,9,17,255});
    DrawCircleGradient(width/2,height/2,static_cast<float>(height)*0.55f,
                       Fade(Color{34,85,105,255},0.25f),Fade(Color{5,9,17,255},0.0f));
    DrawCircleLines(width/2,height/2,static_cast<float>(height)*0.32f,Fade(accent,0.12f));
    DrawCircleLines(width/2,height/2,static_cast<float>(height)*0.40f,Fade(accent,0.08f));
}
}

MenuAction drawStartMenu(bool selectingScene,bool interactive) {
    const float screenWidth=static_cast<float>(GetScreenWidth());
    const float screenHeight=static_cast<float>(GetScreenHeight());
    const float centerX=screenWidth/2.0f;
    const float buttonWidth=std::min(460.0f,screenWidth-80.0f);
    const float buttonX=centerX-buttonWidth/2.0f;

    drawBackground();

    if (!selectingScene) {
        const char* title="Tesseract";
        const int titleSize=64;
        DrawText(title,static_cast<int>(centerX-MeasureText(title,titleSize)/2),
                 static_cast<int>(screenHeight*0.25f),titleSize,ink);
        const char* subtitle="Head-tracked depth window";
        DrawText(subtitle,static_cast<int>(centerX-MeasureText(subtitle,18)/2),
                 static_cast<int>(screenHeight*0.25f+82),18,muted);

        const float buttonHeight=66.0f;
        const float gap=20.0f;
        float buttonY=screenHeight*0.44f;
        MenuAction action=button({buttonX,buttonY,buttonWidth,buttonHeight},
                                 "Select Scene",MenuAction::SelectScene,interactive);
        if (action!=MenuAction::None) return action;
        buttonY+=buttonHeight+gap;
        action=button({buttonX,buttonY,buttonWidth,buttonHeight},
                      "Settings",MenuAction::OpenSettings,interactive);
        if (action!=MenuAction::None) return action;
        buttonY+=buttonHeight+gap;
        return button({buttonX,buttonY,buttonWidth,buttonHeight},
                      "Exit Program",MenuAction::Exit,interactive);
    }

    const char* title="Select a Scene";
    const int titleSize=38;
    DrawText(title,static_cast<int>(centerX-MeasureText(title,titleSize)/2),
             static_cast<int>(screenHeight*0.22f),titleSize,ink);
    const char* subtitle="Choose one of the project's existing raylib scenes.";
    DrawText(subtitle,static_cast<int>(centerX-MeasureText(subtitle,18)/2),
             static_cast<int>(screenHeight*0.22f+58),18,muted);

    float buttonY=screenHeight*0.38f;
    MenuAction action=button({buttonX,buttonY,buttonWidth,64.0f},
                             "Off-axis Floating Shapes",MenuAction::SelectFloatingShapes,interactive);
    if (action!=MenuAction::None) return action;
    buttonY+=82.0f;
    action=button({buttonX,buttonY,buttonWidth,64.0f},
                  "Animated Square",MenuAction::SelectAnimatedSquare,interactive);
    if (action!=MenuAction::None) return action;
    buttonY+=82.0f;
    return button({buttonX,buttonY,buttonWidth,56.0f},"Back to Main Menu",MenuAction::Back,interactive);
}
}
