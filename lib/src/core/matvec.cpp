#include "matvec.hpp"
#include "numerical.hpp"
#include <cstdlib>
#include <cstring>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif
namespace abc::detail {
void matvec_portable(const float *w, const float *x, float *y, int in,
                     int out) {
  for (int i = 0; i < in; ++i) {
    int j = 0;
#if defined(_M_X64) || defined(__x86_64__)
    const auto v = _mm_set1_ps(x[i]);
    for (; j + 4 <= out; j += 4)
      _mm_storeu_ps(y + j,
                    _mm_add_ps(_mm_loadu_ps(y + j),
                               _mm_mul_ps(v, _mm_loadu_ps(w + i * out + j))));
#endif
    for (; j < out; ++j)
      y[j] += x[i] * w[i * out + j];
  }
}
#ifdef ABC_AVX2
void matvec_avx2(const float *, const float *, float *, int, int);
void activation_avx2(float *, int);
void gru_avx2(const float *, const float *, float *);
bool has_avx2() {
#ifdef _MSC_VER
  int r[4];
  __cpuid(r, 1);
  if ((r[2] & (1 << 27)) == 0 || (r[2] & (1 << 28)) == 0 ||
      (_xgetbv(0) & 6) != 6)
    return false;
  __cpuidex(r, 7, 0);
  return (r[1] & (1 << 5)) != 0;
#else
  return __builtin_cpu_supports("avx2");
#endif
}
#endif
Matvec matvec_kernel() {
  static Matvec kernel = []() -> Matvec {
    const char *force = std::getenv("ABC_FORCE_PORTABLE");
    if (force && std::strcmp(force, "1") == 0)
      return matvec_portable;
#ifdef ABC_AVX2
    if (has_avx2())
      return matvec_avx2;
#endif
    return matvec_portable;
  }();
  return kernel;
}
const char *kernel_name() {
  return matvec_kernel() == matvec_portable ? "portable/SSE2" : "AVX2/no-FMA";
}
void motor_activation(float *x, int n) {
#ifdef ABC_AVX2
  if (matvec_kernel() != matvec_portable) {
    activation_avx2(x, n);
    return;
  }
#endif
  for (int i = 0; i < n; ++i)
    x[i] = x[i] * (.5f + .5f * tanh_pade9(.5f * x[i]));
}
void gru_update(const float *p, const float *r, float *h) {
#ifdef ABC_AVX2
  if (matvec_kernel() != matvec_portable) {
    gru_avx2(p, r, h);
    return;
  }
#endif
  for (int j = 0; j < 96; ++j) {
    float reset = .5f + .5f * tanh_pade9(.5f * (p[j] + r[j])),
          update = .5f + .5f * tanh_pade9(.5f * (p[96 + j] + r[96 + j]));
    float candidate = tanh_pade9(p[192 + j] + reset * r[192 + j]);
    h[j] = candidate + (h[j] - candidate) * update;
  }
}
} // namespace abc::detail
