#pragma once
#include <array>
#include <cstdint>
#include <filesystem>

namespace tesseract {
struct WindowFeel {
    double sensitivity = 1.5;
    double smoothing = 0.85; // Fraction retained per 60 Hz render frame.
    double roomDepth = 8;    // Room depth / width = roomDepth / 3.
    double gridOpacity = 0.45;
    int gridDensity = 12;
};
void validateWindowFeel(const WindowFeel& settings);
void saveWindowFeel(const std::filesystem::path& path,const WindowFeel& settings);
WindowFeel loadWindowFeel(const std::filesystem::path& path);
// Bounded image-space head motion; camera depth is intentionally constant.
// No nose/chin rotation or noisy monocular metric Z is used in this mode.
class WindowMotion {
public:
    void observe(std::array<double,2> faceCenter,std::uint64_t sampleId);
    void setMouse(std::array<double,2> normalizedMouse);
    void recenter();
    std::array<double,3> update(double dt,double screenWidth,const WindowFeel& settings);
private:
    std::array<double,2> latest_{0.5,0.5},neutral_{0.5,0.5};
    std::array<double,2> detected_{},smoothed_{};
    double elapsedSinceSample_ = 1.0/30;
    std::uint64_t lastSample_ = 0;
    bool pendingSample_ = false,mouse_ = false;
};
}
