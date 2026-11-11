// Native "open file" dialog for pictures. Returns an empty string if the user cancelled.
#pragma once
#include <string>

namespace tess {

// ownerWindow: raylib's GetWindowHandle(), so the dialog sits on top of our window.
std::string OpenImageFileDialog(void* ownerWindow);

// 3D model files (.glb, .gltf, .obj, ...). Returns the path as UTF-8, empty if cancelled.
std::string OpenModelFileDialog(void* ownerWindow);

} // namespace tess
