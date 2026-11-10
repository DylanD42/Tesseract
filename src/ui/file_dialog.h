// Native "open file" dialog for pictures. Returns an empty string if the user cancelled.
#pragma once
#include <string>

namespace tess {

// ownerWindow: raylib's GetWindowHandle(), so the dialog sits on top of our window.
std::string OpenImageFileDialog(void* ownerWindow);

} // namespace tess
