#pragma once
#include "window_motion.h"
#include <optional>
#include <string>
namespace tesseract {
void drawWindowControls(bool& open,WindowFeel& settings,
                        const std::filesystem::path& settingsPath,std::string& message,
                        bool cameraAvailable,bool cameraActive,bool cameraStarting,
                        std::optional<int>& cameraRequest);
}
