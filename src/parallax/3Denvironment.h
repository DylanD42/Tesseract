#pragma once
#include "tracking_types.h"
#include <filesystem>
#include <memory>
namespace tesseract {
// Owns rendering, the scene and Depth settings. Main thread only.
class Environment3D {
public:
    explicit Environment3D(const std::filesystem::path& applicationDirectory,bool mouseOnly = false);
    ~Environment3D();
    Environment3D(const Environment3D&) = delete;
    Environment3D& operator=(const Environment3D&) = delete;
    bool shouldClose() const;
    void drawFrame(const Snapshot& snapshot);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
