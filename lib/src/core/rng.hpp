#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
namespace abc::detail {
// NumPy RandomState's integer-seeded MT19937, with the reference's conversions.
struct RandomStream {
  std::mt19937 engine;
  explicit RandomStream(std::uint32_t seed = 7) : engine(seed) {}
  void seed(std::uint32_t s) { engine.seed(s); }
  std::uint32_t word() { return engine(); }
  float uniform() { return float(word() & 0xffffffu) * 0x1p-24f; }
  float exponential() {
    const auto hi = word();
    const auto lo = word();
    const std::uint64_t bits = (std::uint64_t(hi) << 32) | lo;
    return float(-std::log1p(-double(bits & ((1ULL << 53) - 1)) * 0x1p-53));
  }
  int categorical(const std::array<float, 16> &p) {
    int chosen = 0;
    float best = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 16; ++i) {
      const float score = p[i] / exponential();
      if ((std::isnan(score) && !std::isnan(best)) || score > best) {
        best = score;
        chosen = i;
      }
    }
    return chosen;
  }
  void discard() {
    for (int i = 0; i < 32; ++i)
      word();
  }
};
void softmax(const float *logits, std::array<float, 16> &probabilities);
} // namespace abc::detail
