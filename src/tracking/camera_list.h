// Lists the video capture devices on this machine so the settings panel can show names, not just numbers.
#pragma once
#include <string>
#include <vector>

namespace tess {

struct CameraInfo {
    int index = 0;          // index to give HeadTracker::startCamera (DirectShow order)
    std::string name;       // e.g. "Logitech BRIO"
};

// Never empty: falls back to a single "Camera 0" entry when nothing can be enumerated.
std::vector<CameraInfo> ListCameras();

} // namespace tess
