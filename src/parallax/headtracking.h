#pragma once
#include "tracking_types.h"
#include <filesystem>
#include <memory>

namespace tesseract {
// Owns camera, YuNet detector and worker. No raylib dependency.
class HeadTracking {
public:
    // Opens only the selected camera index (0-9).
    HeadTracking(const std::filesystem::path& executableDirectory,int cameraIndex);
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
