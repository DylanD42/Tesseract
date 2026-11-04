#pragma once
#include "window_motion.h"
#include <string>
namespace tesseract {
void drawWindowControls(bool& open,WindowFeel& settings,
                        const std::filesystem::path& settingsPath,std::string& message);
}
