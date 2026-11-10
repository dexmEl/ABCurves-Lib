#include "matvec.hpp"
#include <immintrin.h>
namespace abc::detail {
namespace {
__m256 set(float x) { return _mm256_set1_ps(x); }
__m256 tanh9(__m256 v) {
  auto x = _mm256_min_ps(_mm256_max_ps(v, set(-7)), set(7));
  auto z = _mm256_mul_ps(x, x);
  auto n = _mm256_add_ps(set(990), z);
  n = _mm256_add_ps(set(135135), _mm256_mul_ps(z, n));
  n = _mm256_add_ps(set(4729725), _mm256_mul_ps(z, n));
  n = _mm256_mul_ps(x, _mm256_add_ps(set(34459425.f), _mm256_mul_ps(z, n)));
  auto d = _mm256_add_ps(set(13860), _mm256_mul_ps(set(45), z));
  d = _mm256_add_ps(set(945945), _mm256_mul_ps(z, d));
  d = _mm256_add_ps(set(16216200), _mm256_mul_ps(z, d));
  d = _mm256_add_ps(set(34459425.f), _mm256_mul_ps(z, d));
  return _mm256_min_ps(_mm256_max_ps(_mm256_div_ps(n, d), set(-1)), set(1));
}
__m256 sigmoid9(__m256 x) {
  return _mm256_add_ps(
      set(.5f), _mm256_mul_ps(set(.5f), tanh9(_mm256_mul_ps(set(.5f), x))));
}
} // namespace
void matvec_avx2(const float *w, const float *x, float *y, int in, int out) {
  int j = 0;
  // Keep 32 independent output accumulators in registers across the reduction.
  // Every output still adds inputs in the original ascending order.
  for (; j + 32 <= out; j += 32) {
    auto a = _mm256_loadu_ps(y + j), b = _mm256_loadu_ps(y + j + 8);
    auto c = _mm256_loadu_ps(y + j + 16), d = _mm256_loadu_ps(y + j + 24);
    for (int i = 0; i < in; ++i) {
      auto v = _mm256_set1_ps(x[i]);
      const float *row = w + i * out + j;
      a = _mm256_add_ps(a, _mm256_mul_ps(v, _mm256_loadu_ps(row)));
      b = _mm256_add_ps(b, _mm256_mul_ps(v, _mm256_loadu_ps(row + 8)));
      c = _mm256_add_ps(c, _mm256_mul_ps(v, _mm256_loadu_ps(row + 16)));
      d = _mm256_add_ps(d, _mm256_mul_ps(v, _mm256_loadu_ps(row + 24)));
    }
    _mm256_storeu_ps(y + j, a);
    _mm256_storeu_ps(y + j + 8, b);
    _mm256_storeu_ps(y + j + 16, c);
    _mm256_storeu_ps(y + j + 24, d);
  }
  for (; j + 8 <= out; j += 8) {
    auto a = _mm256_loadu_ps(y + j);
    for (int i = 0; i < in; ++i)
      a = _mm256_add_ps(a, _mm256_mul_ps(_mm256_set1_ps(x[i]),
                                         _mm256_loadu_ps(w + i * out + j)));
    _mm256_storeu_ps(y + j, a);
  }
  for (; j < out; ++j)
    for (int i = 0; i < in; ++i)
      y[j] += x[i] * w[i * out + j];
}
void activation_avx2(float *x, int n) {
  for (int i = 0; i < n; i += 8) {
    auto v = _mm256_loadu_ps(x + i);
    _mm256_storeu_ps(x + i, _mm256_mul_ps(v, sigmoid9(v)));
  }
}
void gru_avx2(const float *p, const float *r, float *h) {
  for (int i = 0; i < 96; i += 8) {
    auto reset =
        sigmoid9(_mm256_add_ps(_mm256_loadu_ps(p + i), _mm256_loadu_ps(r + i)));
    auto update = sigmoid9(_mm256_add_ps(_mm256_loadu_ps(p + 96 + i),
                                         _mm256_loadu_ps(r + 96 + i)));
    auto candidate = tanh9(
        _mm256_add_ps(_mm256_loadu_ps(p + 192 + i),
                      _mm256_mul_ps(reset, _mm256_loadu_ps(r + 192 + i))));
    auto prior = _mm256_loadu_ps(h + i);
    _mm256_storeu_ps(
        h + i,
        _mm256_add_ps(candidate,
                      _mm256_mul_ps(_mm256_sub_ps(prior, candidate), update)));
  }
}
} // namespace abc::detail
