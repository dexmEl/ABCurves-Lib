#pragma once
namespace abc::detail {
// Column-contiguous matrix. Accumulation order within each output is fixed;
// explicit SIMD operates across independent outputs, never a horizontal sum.
using Matvec = void (*)(const float *, const float *, float *, int, int);
Matvec matvec_kernel();
const char *kernel_name();
void motor_activation(float *, int);
void gru_update(const float *, const float *, float *);
void matvec_portable(const float *, const float *, float *, int, int);
} // namespace abc::detail
