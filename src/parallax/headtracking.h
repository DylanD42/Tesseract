#pragma once
#include "tracking_types.h"
#include <filesystem>
#include <memory>

namespace tesseract {
// Owns camera, YuNet detector, diagnostics window and worker. No raylib dependency.
class HeadTracking {
public:
    // -1 scans 0-9. An explicit index never silently selects another device.
    HeadTracking(const std::filesystem::path& executableDirectory,int cameraIndex = -1);
    ~HeadTracking();
    HeadTracking(const HeadTracking&) = delete;
    HeadTracking& operator=(const HeadTracking&) = delete;
    Snapshot snapshot() const;
    bool stopRequested() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
