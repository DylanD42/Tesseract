#include "headtracking.h"
#include "3Denvironment.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::filesystem::path executableDirectory() {
    // Grow the buffer so long installation paths are supported.
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) throw std::runtime_error("Cannot locate executable directory");
        if (length < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
}
int cameraIndex(int argc, char** argv) {
    if (argc == 1) return -1;
    if (argc != 2) throw std::invalid_argument("Usage: TesseractParallax.exe [camera-index | --mouse]");
    const std::string value = argv[1];
    std::size_t parsed = 0;
    const int index = std::stoi(value, &parsed);
    if (parsed != value.size() || index < 0)
        throw std::invalid_argument("Camera index must be a nonnegative integer");
    return index;
}
}

int main(int argc, char** argv) {
    try {
        const bool mouseOnly = argc==2 && std::string(argv[1])=="--mouse";
        const int index = mouseOnly ? -1 : cameraIndex(argc, argv);
        const auto directory = executableDirectory();
        tesseract::Environment3D environment(directory,mouseOnly);
        std::unique_ptr<tesseract::HeadTracking> tracking;
        if (!mouseOnly) {
            tracking=std::make_unique<tesseract::HeadTracking>(directory,index);
        }
        while (!environment.shouldClose() && (!tracking || !tracking->stopRequested())) {
            const auto snapshot=tracking?tracking->snapshot():tesseract::Snapshot{};
            environment.drawFrame(snapshot);
        }
        // Destruction stops and joins tracking before closing the rendering window.
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
