// printf-style formatting into a std::string (so UI code does not need cv::format).
#pragma once
#include <string>

namespace tess {

std::string Fmt(const char* fmt, ...);

} // namespace tess
