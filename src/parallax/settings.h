#pragma once
namespace tesseract::settings {
inline constexpr int kWinW=1000,kWinH=800;
inline constexpr float kWindowOpeningWidth=30.0f;
inline constexpr float kScreenHeightUnits=10.0f; // Height of the optional solid comparison room.
inline constexpr float kScreenPlaneZ=1.0f,kScreenCenterY=1.0f;
inline constexpr int kDetectWidth=320;
inline constexpr float kYuNetScoreThreshold=0.7f,kYuNetNmsThreshold=0.3f;
inline constexpr int kYuNetTopK=50;
}
