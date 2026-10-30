#pragma once
#include "tracking_types.h"
#include <array>
namespace tesseract::ui {
struct Actions {bool recenter=false;bool toggleFullscreen=false;};
Actions pollActions();
void drawOverlay(const Snapshot& snapshot,const std::array<float,3>& viewerPosition);
}
