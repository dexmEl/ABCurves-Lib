#include "matvec.hpp"
#include "numerical.hpp"
#include "failure.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace abc::detail {
#ifdef ABC_PROFILE
Profile &profile() {
  static thread_local Profile p;
  return p;
}
#endif
namespace {
template <class T>
void copy_tensor(const Model &m, const char *name, T *data,
                 std::initializer_list<std::uint32_t> shape) {
  auto &t = m.tensor(name);
  std::size_t n = 1;
  for (auto x : shape)
    n *= x;
  if (t.dtype != (sizeof(T) == 4 ? 1 : 2) ||
      t.shape != std::vector<std::uint32_t>(shape) || t.bytes != n * sizeof(T))
    throw std::runtime_error(std::string("incompatible tensor: ") + name);
  std::memcpy(data, t.data, t.bytes);
}
float sig_pade(float x) { return .5f + .5f * tanh_pade9(.5f * x); }
} // namespace
float sigmoid_exact(float x) {
  float z = std::exp(-std::abs(x));
  return x >= 0 ? 1.f / (1.f + z) : z / (1.f + z);
}
float tanh_pade9(float value) {
  float x = std::clamp(value, -7.f, 7.f), z = x * x;
  float n =
      x * (34459425.f + z * (4729725.f + z * (135135.f + z * (990.f + z))));
  float d =
      34459425.f + z * (16216200.f + z * (945945.f + z * (13860.f + 45.f * z)));
  return std::clamp(n / d, -1.f, 1.f);
}
void softmax(const float *x, std::array<float, 16> &p) {
  float mx = *std::max_element(x, x + 16);
  for (int i = 0; i < 16; ++i)
    p[i] = std::exp(x[i] - mx);
  // NumPy's pairwise float32 reduction for a contiguous 16-element vector.
  float r[8];
  for (int j = 0; j < 8; ++j)
    r[j] = p[j] + p[j + 8];
  float total =
      ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
  if (!std::isfinite(total) || total <= 0)
    throw GenerationError(FailureCategory::inference, FailureReason::invalid_probabilities,
                          "invalid categorical probabilities");
  for (auto &v : p)
    v /= total;
}
Linear::Linear(const Model &m, const char *weight, const char *bias, int input,
               int output)
    : in(input), out(output), w(input * output), b(output),
      transpose(input * output) {
  copy_tensor(m, weight, w.data(), {std::uint32_t(out), std::uint32_t(in)});
  copy_tensor(m, bias, b.data(), {std::uint32_t(out)});
  for (int i = 0; i < in; ++i)
    for (int j = 0; j < out; ++j)
      transpose[i * out + j] = w[j * in + i];
}
void Linear::run(const float *x, float *y, bool activation) const {
  std::fill_n(y, out, 0.f);
  matvec_kernel()(transpose.data(), x, y, in, out);
  for (int i = 0; i < out; ++i) {
    float sum = y[i] + b[i];
    y[i] = activation ? sum * sigmoid_exact(sum) : sum;
  }
}
Weights::Weights(const Model &m)
    : input(m, "motor.input.0.weight", "motor.input.0.bias", 9, 96),
      ih(m, "motor.encoder.weight_ih_l0", "motor.encoder.bias_ih_l0", 96, 288),
      hh(m, "motor.encoder.weight_hh_l0", "motor.encoder.bias_hh_l0", 96, 288),
      trunk(m, "motor.trunk.0.weight", "motor.trunk.0.bias", 158, 96),
      output(m, "motor.output.weight", "motor.output.bias", 96, 672),
      unary(m, "motor.selection.unary.0.weight", "motor.selection.unary.0.bias",
            112, 64),
      unary_out(m, "motor.selection.unary.2.weight",
                "motor.selection.unary.2.bias", 64, 1),
      context(m, "motor.selection.context.0.weight",
              "motor.selection.context.0.bias", 96, 16),
      pair(m, "motor.selection.pair.0.weight", "motor.selection.pair.0.bias",
           36, 32),
      pair_out(m, "motor.selection.pair.2.weight",
               "motor.selection.pair.2.bias", 32, 1),
      hazard0(m, "events.hazard.0.weight", "events.hazard.0.bias", 32, 64),
      hazard2(m, "events.hazard.2.weight", "events.hazard.2.bias", 64, 32),
      hazard4(m, "events.hazard.4.weight", "events.hazard.4.bias", 32, 2),
      brake0(m, "events.encoder.0.weight", "events.encoder.0.bias", 32, 96),
      brake2(m, "events.encoder.2.weight", "events.encoder.2.bias", 96, 96),
      brake_out(m, "events.brake.weight", "events.brake.bias", 96, 176),
      frequency(m, "events.frequency.weight", "events.frequency.bias", 96, 16) {
  copy_tensor(m, "motor.velocity_basis", velocity.data(), {128, 21});
  copy_tensor(m, "motor.carry_velocity", carry.data(), {128});
  for (int i = 0; i < 16; ++i) {
    for (int j = 0; j < 21; ++j) {
      double sum = 0;
      for (int k = 0; k < 4; ++k)
        sum += velocity[(i * 4 + k) * 21 + j];
      mean_basis[i * 21 + j] = sum / 4.;
    }
    double sum = 0;
    for (int k = 0; k < 4; ++k)
      sum += carry[i * 4 + k];
    mean_carry[i] = sum / 4.;
  }
}
void Neural::motor(const Features &f) {
  {
    ABC_TIME(input_stage);
    for (int r = 0; r < 70; ++r) {
      w->input.run(f.coarse.data() + r * 9, tokens.data() + r * 96);
      motor_activation(tokens.data() + r * 96, 96);
    }
  }
  {
    ABC_TIME(projection_stage);
    for (int r = 0; r < 70; ++r)
      w->ih.run(tokens.data() + r * 96, projected.data() + r * 288);
  }
  {
    ABC_TIME(recurrence_stage);
    hidden.fill(0);
    for (int r = 0; r < 70; ++r) {
      // Input-major order preserves the Python vector recurrence reduction.
      std::copy(w->hh.b.begin(), w->hh.b.end(), recurrent.begin());
      matvec_kernel()(w->hh.transpose.data(), hidden.data(), recurrent.data(),
                      96, 288);
      gru_update(projected.data() + r * 288, recurrent.data(), hidden.data());
      if (state_trace)
        std::copy(hidden.begin(), hidden.end(), state_trace + r * 96);
    }
  }
  {
    ABC_TIME(trunk_stage);
    std::array<float, 158> combined;
    std::copy(hidden.begin(), hidden.end(), combined.begin());
    std::copy(f.fine.begin(), f.fine.end(), combined.begin() + 96);
    std::copy(f.dynamics.begin(), f.dynamics.end(), combined.begin() + 150);
    w->trunk.run(combined.data(), encoded.data());
    motor_activation(encoded.data(), 96);
    w->output.run(encoded.data(), coefficients.data());
  }
}
void Neural::geometry(Vec2 incoming) {
  ABC_TIME(geometry_stage);
  for (int h = 0; h < 16; ++h)
    for (int i = 0; i < 16; ++i)
      for (int d = 0; d < 2; ++d) {
        double sum = 0;
        for (int j = 0; j < 21; ++j)
          sum += w->mean_basis[i * 21 + j] *
                 double(coefficients[h * 42 + j * 2 + d]);
        sum += w->mean_carry[i] * (d ? incoming.y : incoming.x);
        heads[h * 32 + i * 2 + d] = float(sum);
      }
}
void Neural::selector(const std::array<float, 32> &previous, bool valid,
                      Vec2 actual, int interval) {
  ABC_TIME(selector_stage);
  {
    ABC_TIME(selector_features_stage);
    pairs.fill(0);
    int shift = interval / 4;
    float ps[2]{}, prefix[2]{};
    if (valid)
      for (int d = 0; d < 2; ++d)
        for (int i = 0; i < 16; ++i) {
          ps[d] += previous[2 * i + d];
          if (i < shift)
            prefix[d] += previous[2 * i + d];
        }
    for (int h = 0; h < 16; ++h) {
      for (int i = 0; i < 16; ++i) {
        unary_geometry[h * 16 + i] = std::asinh(heads[h * 32 + i]);
        if (valid)
          pairs[h * 20 + i] =
              std::asinh(previous[shift * 2 + i] - heads[h * 32 + i]);
      }
      if (valid)
        for (int d = 0; d < 2; ++d) {
          float total = 0;
          for (int i = 0; i < 8; ++i)
            total += heads[h * 32 + 2 * i + d];
          float a = d ? float(actual.y) : float(actual.x);
          float endpoint = ps[d] * 4.f - a;
          endpoint = endpoint - total * 4.f;
          endpoint = endpoint / 32.f;
          float feedback = prefix[d] * 4.f - a;
          feedback = feedback / float(interval);
          pairs[h * 20 + 16 + d] = std::asinh(endpoint);
          pairs[h * 20 + 18 + d] = std::asinh(feedback);
        }
    }
  }
  ABC_TIME(selector_network_stage);
  // Split the shared latent projection exactly as the reference small network.
  std::array<float, 64> shared{}, latent{};
  matvec_kernel()(w->unary.transpose.data(), encoded.data(), shared.data(), 96,
                  64);
  for (int i = 0; i < 64; ++i)
    shared[i] += w->unary.b[i];
  std::array<float, 16> context{};
  std::array<float, 32> shared_pair{}, pair_hidden{};
  if (valid) {
    w->context.run(encoded.data(), context.data(), true);
    matvec_kernel()(w->pair.transpose.data() + 20 * 32, context.data(),
                    shared_pair.data(), 16, 32);
    for (int i = 0; i < 32; ++i)
      shared_pair[i] += w->pair.b[i];
  }
  for (int h = 0; h < 16; ++h) {
    latent.fill(0);
    matvec_kernel()(w->unary.transpose.data() + 96 * 64,
                    unary_geometry.data() + h * 16, latent.data(), 16, 64);
    for (int i = 0; i < 64; ++i) {
      float x = shared[i] + latent[i];
      latent[i] = x * sigmoid_exact(x);
    }
    w->unary_out.run(latent.data(), &logits[h]);
    if (valid) {
      pair_hidden.fill(0);
      matvec_kernel()(w->pair.transpose.data(), pairs.data() + h * 20,
                      pair_hidden.data(), 20, 32);
      for (int i = 0; i < 32; ++i) {
        float x = shared_pair[i] + pair_hidden[i];
        pair_hidden[i] = x * sigmoid_exact(x);
      }
      float transition;
      w->pair_out.run(pair_hidden.data(), &transition);
      logits[h] += transition;
    }
  }
  softmax(logits.data(), probabilities);
}
void Neural::hazards(const std::array<float, 32> &x) {
  ABC_TIME(hazard_stage);
  std::array<float, 64> a;
  std::array<float, 32> b;
  w->hazard0.run(x.data(), a.data(), true);
  w->hazard2.run(a.data(), b.data(), true);
  w->hazard4.run(b.data(), hazard.data());
}
void Neural::brake(const std::array<float, 32> &x) {
  ABC_TIME(brake_stage);
  std::array<float, 96> a, b;
  std::array<float, 176> p;
  w->brake0.run(x.data(), a.data(), true);
  w->brake2.run(a.data(), b.data(), true);
  w->brake_out.run(b.data(), p.data());
  for (int h = 0; h < 16; ++h) {
    duration[h] = std::exp(std::clamp(p[11 * h], float(1.3862943611198906),
                                      float(5.2574953720277815)));
    for (int j = 0; j < 10; ++j)
      brake_coeff[h * 10 + j] = 64.f * p[11 * h + 1 + j];
  }
  w->frequency.run(b.data(), frequency.data());
}
void Neural::decode(int chosen, Vec2 incoming, int count, Vec2 *action) const {
  ABC_TIME(decode_stage);
  for (int i = 0; i < count; ++i) {
    Vec2 v{};
    for (int j = 0; j < 21; ++j) {
      v.x +=
          w->velocity[i * 21 + j] * double(coefficients[chosen * 42 + 2 * j]);
      v.y += w->velocity[i * 21 + j] *
             double(coefficients[chosen * 42 + 2 * j + 1]);
    }
    action[i] = v + incoming * w->carry[i];
  }
}
} // namespace abc::detail
