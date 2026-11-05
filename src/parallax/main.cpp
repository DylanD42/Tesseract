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
}

int main(int argc, char** argv) {
    try {
        if (argc>2||(argc==2&&std::string(argv[1])!="--mouse"))
            throw std::invalid_argument("Usage: TesseractParallax.exe [--mouse]");
        const bool mouseOnly = argc==2 && std::string(argv[1])=="--mouse";
        const auto directory = executableDirectory();
        tesseract::Environment3D environment(directory,mouseOnly);
        std::unique_ptr<tesseract::HeadTracking> tracking;

        while (!environment.shouldClose()) {
            tesseract::Snapshot snapshot;
            if (tracking&&tracking->stopRequested()) {
                snapshot=tracking->snapshot();
                const std::string status=snapshot.status.empty()?"Tracking stopped":snapshot.status;
                tracking.reset();
                environment.setCameraStatus(false,status);
            } else if (tracking) {
                snapshot=tracking->snapshot();
            }

            environment.drawFrame(snapshot);

            const auto cameraRequest=environment.takeCameraRequest();
            if (cameraRequest) {
                if (*cameraRequest<0) {
                    tracking.reset();
                    environment.setCameraStatus(false,"Camera stopped; mouse preview is active.");
                } else {
                    try {
                        tracking=std::make_unique<tesseract::HeadTracking>(directory,*cameraRequest);
                        environment.setCameraStatus(true,"Camera "+std::to_string(*cameraRequest)+" connected.");
                    } catch (const std::exception& error) {
                        tracking.reset();
                        std::cerr << error.what() << '\n';
                        environment.setCameraStatus(false,error.what());
                    }
                }
            }
        }
        // Destruction stops and joins tracking before closing the rendering window.
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
