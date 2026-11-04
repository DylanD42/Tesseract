#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace tesseract {
// Copies cross the tracking/render boundary. Coordinates are normalized image
// positions, not estimated millimeters or head/gaze rotation.
struct Snapshot {
    bool faceCenterValid = false;
    std::array<double,2> faceCenter{0.5,0.5};
    std::uint64_t faceSampleId = 0;
    int faceCount = 0;
    double faceScore = 0;
    std::string status = "Starting camera";
};
}
