#include "core/text.h"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace tess {

std::string Fmt(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) { va_end(copy); return std::string(); }
    if (size_t(n) < sizeof(buf)) { va_end(copy); return std::string(buf, size_t(n)); }
    std::vector<char> big(size_t(n) + 1);
    std::vsnprintf(big.data(), big.size(), fmt, copy);
    va_end(copy);
    return std::string(big.data(), size_t(n));
}

} // namespace tess
