// This file must not include raylib.h (it clashes with windows.h).
#include "ui/file_dialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace tess {

#ifdef _WIN32
std::string OpenImageFileDialog(void* ownerWindow) {
    char file[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(ownerWindow);
    ofn.lpstrFilter = "Images (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Choose the photo of your screen";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return {};
    return file;
}

std::string OpenModelFileDialog(void* ownerWindow) {
    wchar_t file[4096] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(ownerWindow);
    ofn.lpstrFilter = L"3D models (*.glb;*.gltf;*.obj;*.iqm;*.vox;*.m3d)\0*.glb;*.gltf;*.obj;*.iqm;*.vox;*.m3d\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = L"Import a 3D model (from Blender: File > Export > glTF 2.0 .glb)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, &out[0], n, nullptr, nullptr);
    return out;
}
#else
std::string OpenImageFileDialog(void*) { return {}; }
std::string OpenModelFileDialog(void*) { return {}; }
#endif

} // namespace tess
