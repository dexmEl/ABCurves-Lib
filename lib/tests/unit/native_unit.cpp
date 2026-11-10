#include "abcurves/native.hpp"

#include <cassert>
#include <cmath>

int main() {
    static_assert(abc::COMMON_RADIANS_PER_COUNT > 0.0);
    assert(abc::COMMON_RADIANS_PER_COUNT == 0.0003509487083280618);
    const abc::Vec2 a{1.0, 2.0};
    const abc::Vec2 b{3.0, -2.0};
    const auto c = a + b;
    assert(c.x == 4.0 && c.y == 0.0);
    assert(std::isfinite(abc::COMMON_RADIANS_PER_COUNT));
    return 0;
}
